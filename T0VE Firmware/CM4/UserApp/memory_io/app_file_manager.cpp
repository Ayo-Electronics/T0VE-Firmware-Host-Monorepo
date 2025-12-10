/*
 * app_file_manager.cpp
 *
 *  Created on: Dec 8, 2025
 *      Author: govis
 *
 */

#include "app_file_manager.hpp"

//===================================================== PUBLIC METHODS =====================================================

//init function, spawns monitoring thread
void File_Manager::init() {
	check_file_request_task.schedule_interval_ms(BIND_CALLBACK(this, check_file_request), Scheduler::INTERVAL_EVERY_ITERATION);
}

//attach method looks for the first invalid file and drops it there
void File_Manager::attach_file(Basic_File& _file) {
	//detach the file first
	detach_file(_file);

	//and drop the file in the first invalid spot
	for(size_t i = 0; i < files.size(); i++) {
		if(!files[i].is_valid()) {
			files[i] = _file;
			return;
		}
	}

	//if we got here, we couldn't find a spot for the file
	Debug::WARN("File_Manager::attach_file: No space to store file!");
}

void File_Manager::detach_file(Basic_File& _file) {
	//go through all files
	for(size_t i = 0; i < files.size(); i++) {
		//check if our files match using basic_file's equality comparison
		//checks to see if we're referencing identical memory
		if(files[i] == _file) {
			//if we do match, replace that file with a blank one (invalid) and return
			files[i] = Basic_File();
			return;
		}
	}

	//if we got here, we didn't find the file in our list, but that may be expected
	//to reduce debug clutter, don't log any messages here
}

//===================================================== PRIVATE METHODS ===========================================================

//thread function, listens for protobuf requests regarding memory access
void File_Manager::check_file_request() {
	if(comms_mem_access_outbound.check()) {
		//we have a new message from the comms system
		//dispatch to the appropriate handler based on type
		auto msg = comms_mem_access_outbound.read();

		//if the payload is a file request, pop it over to the appropriate handler
		if(msg.which_payload == app_Neural_Mem_FileRequest_file_access_tag)
			handle_file_access(msg.payload.file_access);

		//or if the payload is a file list request
		if(msg.which_payload == app_Neural_Mem_FileRequest_file_list_tag)
			handle_file_report();
	}
}

//handles memory access requests
void File_Manager::handle_file_access(app_Neural_Mem_FileAccess& access_command) {
	//output temporary, mirrors the command
	access_response = access_command;
	size_t transfer_size = 0;

	//request to perform a memory read
	if(access_command.read_nwrite) {
		//attempt the read
		auto resp_span = std::span<uint8_t, std::dynamic_extent>(
			reinterpret_cast<uint8_t*>(access_response.data.bytes),	//read into our response directly
			static_cast<size_t>(access_command.data.size)			//but only read the number of bytes we were commanded
		);
		transfer_size = read_file_segment(
			access_command.filename,
			access_command.offset,
			resp_span
		);
	}

	//request to perform a memory write
	else {
		//attempt the read
		auto cmd_span = std::span<uint8_t, std::dynamic_extent>(
			reinterpret_cast<uint8_t*>(access_command.data.bytes),	//write from our command directly
			static_cast<size_t>(access_command.data.size)			//but only write the number of bytes we were commanded
		);
		transfer_size = write_file_segment(
			access_command.filename,
			access_command.offset,
			cmd_span
		);
	}

	//set the size field of response to the size we received
	//size == 0 indicates read/write fail
	access_response.data.size = transfer_size;

	//pack into a neural memory response
	app_Neural_Mem_FileRequest packed_resp = app_Neural_Mem_FileRequest_init_zero;
	packed_resp.which_payload = app_Neural_Mem_FileRequest_file_access_tag;
	packed_resp.payload.file_access = access_response;

	//push the response to the comms system
	comms_mem_access_inbound.publish_unconditional(packed_resp);
}

//report the list of files we currently have attached
void File_Manager::handle_file_report() {
	//create an output temporary, grab the part of the payload that we care about
	app_Neural_Mem_FileRequest packed_resp = app_Neural_Mem_FileRequest_init_zero;
	packed_resp.which_payload = app_Neural_Mem_FileRequest_file_list_tag;
	auto& file_report = packed_resp.payload.file_list;

	//go through all elements of our maintained files
	size_t max_report_files = sizeof(file_report.files)/sizeof(file_report.files[0]);
	size_t file_count = 0;
	for(auto& file : files) {
		//if the file at the particular index is valid
		//add its name and size to our report
		if(file.is_valid()) {
			//copy over the name
			auto& name_dest = file_report.files[file_count].filename;
			auto name_source = file.get_file_name();
			memcpy(name_dest, name_source.span().data(), name_source.span().size());

			//and save the file size
			file_report.files[file_count].filesize = file.get_file_size();

			//increment the report index, break if we can't hold more
			file_count++;
			if(file_count >= max_report_files) break;
		}
	}

	//push the response to the comms system
	comms_mem_access_inbound.publish_unconditional(packed_resp);
}

//private methods to read/write to the various files
//returns number of bytes read, 0 if method fails
size_t File_Manager::read_file_segment(	App_String<Basic_File::FILENAME_MAX_LENGTH> file_name,
										uint32_t offset,
										std::span<uint8_t, std::dynamic_extent> data)
{
	//go through our files, check if one of them matches the filename
	//use the built-in string comparison operator
	for(auto& file : files) {
		//if filenames match, attempt a read from the file segment
		if(file_name == file.get_file_name()) {
			return file.read(offset, data);
		}
	}

	//otherwise we didn't find the file, no bytes read
	return 0;

}

//if this method fails, data will be an empty span with a null pointer
//returns number of bytes written, 0 if method fails
size_t File_Manager::write_file_segment(	App_String<Basic_File::FILENAME_MAX_LENGTH> file_name,
											uint32_t offset,
											std::span<uint8_t, std::dynamic_extent> data)
{
	//go through our files, check if one of them matches the filename
	//use the built-in string comparison operator
	for(auto& file : files) {
		//if filenames match, attempt a write to the file segment
		if(file_name == file.get_file_name()) {
			return file.write(offset, data);
		}
	}

	//otherwise we didn't find the file, no bytes written
	return 0;
}
