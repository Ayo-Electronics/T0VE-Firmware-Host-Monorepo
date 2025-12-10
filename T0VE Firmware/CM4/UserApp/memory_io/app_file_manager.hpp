/*
 * app_file_manager.hpp
 *
 *  Created on: Dec 8, 2025
 *      Author: govis
 *
 *  As an alternative to the MSC Interface
 *  Read/Write files via a protobuf interface; effectively emulates SCSI read10 and write10 commands
 *
 *	TODO: how to report file names?
 */

#pragma once

#include "app_string.hpp"
#include "app_proctypes.hpp"
#include "app_utils.hpp"
#include "app_basic_file.hpp"
#include "app_debug_if.hpp"
#include "app_scheduler.hpp"

#include "app_messages.pb.h"

class File_Manager {

public:

	//========================= TYPEDEFS AND CONSTANTS ========================
	static const size_t MAX_NUM_FILES = 8;

	//================================ METHODS ================================
	//empty constructor
	File_Manager() {}

	//initializer just sets up main thread function
	void init();

	//attach/detach file methods
	//look identical to the MSC_if methods
	void attach_file(Basic_File& _file);
	void detach_file(Basic_File& _file);

	//add the read/write ports
	SUBSCRIBE_FUNC(	comms_mem_access_inbound	);
	LINK_FUNC(		comms_mem_access_outbound	);

	//delete assignment operator and copy constructor
	File_Manager(const File_Manager& other) = delete;
	void operator=(const File_Manager& other) = delete;

private:
	//==================== PRIVATE FUNCTIONS =======================
	//main thread function
	//checks for file system requests via the protobuf ports; responds appropriately
	void check_file_request();
	Scheduler check_file_request_task;

	//handle protobuf file report request
	void handle_file_report();

	//handle protobuf file access (read/write) operation
	void handle_file_access(app_Neural_Mem_FileAccess& access_command);

	//private methods to read/write to the various files
	//returns number of bytes read, 0 if method fails
	size_t read_file_segment(	App_String<Basic_File::FILENAME_MAX_LENGTH> file_name,
								uint32_t offset,
								std::span<uint8_t, std::dynamic_extent> data);

	//if this method fails, data will be an empty span with a null pointer
	//returns number of bytes written, 0 if method fails
	size_t write_file_segment(	App_String<Basic_File::FILENAME_MAX_LENGTH> file_name,
								uint32_t offset,
								std::span<uint8_t, std::dynamic_extent> data);

	//===================== PRIVATE VARIABLES ====================
	//have an array of files, default initialize them
	std::array<Basic_File, MAX_NUM_FILES> files;

	//and have some i/o ports for protobuf messages
	PERSISTENT((Pub_Var<app_Neural_Mem_FileRequest>), comms_mem_access_inbound);
	Sub_Var<app_Neural_Mem_FileRequest> comms_mem_access_outbound;

	//have read responses/write response buffer permanently allocated to avoid stack traffic during reads/writes
	//will get copied on publish regardless so doesn't matter if this gets edited
	app_Neural_Mem_FileAccess access_response;
};
