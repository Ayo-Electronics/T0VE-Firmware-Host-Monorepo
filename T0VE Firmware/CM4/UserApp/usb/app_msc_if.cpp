
/*
 * app_msc_if.cpp
 *
 *  Created on: Sep 19, 2025
 *      Author: govis
 */

#include "app_msc_if.hpp"

//================= STATIC MEMBERS ===============
MSC_Interface::MSC_Interface_Channel_t MSC_Interface::MSC_CHANNEL = {
		.lun_no = 0,
		.msc_if = nullptr
};

//================= PUBLIC FUNCTIONS =============

MSC_Interface::MSC_Interface(USB_Interface& _usb_if, MSC_Interface_Channel_t& channel):
		usb_if(_usb_if), msc_channel(channel)
{
	//register our channel with this particular instance
	msc_channel.msc_if = this;
}

void MSC_Interface::init() {
    //call the upstream init function
	usb_if.init();

	//and generate our tables using the default values
	regenerate();
}

void MSC_Interface::notify_file_change() {
	//just assert the flag here; logic handled in `ready` function
	file_changed = true;
}

void MSC_Interface::connect_request() {
    //call the upstream connect_request function
	usb_if.connect_request();

	//and say that we're accessible here (sets how we respond to some SCSI requests)
	accessible = true;
}

void MSC_Interface::disconnect_request() {
    //call the upstream disconnect_request function
	usb_if.disconnect_request();

	//and mark that we're inaccessible (sets how we respond to some SCSI requests)
	accessible = false;
}

void MSC_Interface::set_string_fields(	const App_String<11, ' '>	_vol_name,
										const App_String<8, ' '>  	_scsi_vid,
										const App_String<16, ' '>  	_scsi_pid,
										const App_String<4, ' '>  	_scsi_rev	)
{
	//copy our strings
	vol_name = _vol_name;
	scsi_pid = _scsi_pid;
	scsi_vid = _scsi_vid;
	scsi_rev = _scsi_rev;

	//and regenerate our table based on our strings
	regenerate();
}

void MSC_Interface::attach_file(MSC_File& _file) {
    //start by detaching the file
	//won't do anything if we aren't managing that file yet
	detach_file(_file);

	//then just push the file to the back of the container
	msc_files.push_back(_file);
    //regenerate tables to reflect the newly attached file
    regenerate();
}

void MSC_Interface::detach_file(MSC_File& _file) {
	//go through all of our files and check if the file matches
	for(size_t i = 0; i < msc_files.size(); i++) {

		//if we find a file that matches,
		//remove it and regenerate all our tables
		if(_file == msc_files[i]) {
			msc_files.erase(i);
			regenerate();
			return;
		}
	}
}


//================= PRIVATE FUNCTIONS =============
//########### FS REGENERATION AFTER CHANGES ############
void MSC_Interface::regenerate() {
	//have to update our FAT tabel first for the file clusters
	auto file_clusters = fat_table.mk(msc_files.span());

	//then boot sector, then root sector
	boot_sector.mk(vol_name); //default UID for now,
	root_sector.mk(vol_name, msc_files.span(), file_clusters);

	//update the data sector dispatcher with latest file cluster indices
	data_sector.mk(msc_files.span(), file_clusters);
}

//############ TINYUSB HANDLERS ###########
uint32_t MSC_Interface::handle_msc_inquiry(scsi_inquiry_resp_t *inquiry_resp, uint32_t bufsize) {
	//copy the SCSI strings to the inquiry response
	memcpy(inquiry_resp->vendor_id, scsi_vid.array().data(), scsi_vid.array().size());
	memcpy(inquiry_resp->product_id, scsi_pid.array().data(), scsi_pid.array().size());
	memcpy(inquiry_resp->product_rev, scsi_rev.array().data(), scsi_rev.array().size());

	return sizeof(scsi_inquiry_resp_t);
}

//return true if host can read/write to the device; return false if ejected
bool MSC_Interface::handle_msc_ready() {
	//if we were made inaccessible
	//send this info to the MSC driver too (copied from example)
	if(!accessible) {
		tud_msc_set_sense(msc_channel.lun_no, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
		return false;
	}

	//also check if one of our files changed
	//notify the host that they should re-scan
	if(file_changed) {
		tud_msc_set_sense(msc_channel.lun_no, SCSI_SENSE_UNIT_ATTENTION, 0x28, 0x00);
		file_changed = false;	//clear our flag
		return false;			//and signal that we aren't ready
	}

	//and return if we'd like to be accessible
	return true;
}

//use constants --> block size = sector size; block count = total #sectors
void MSC_Interface::handle_msc_capacity(uint32_t* block_count, uint16_t* block_size) {
	//use our file system constants to respond to this
	*block_count = FS_Constants::TOTAL_NUM_SECTORS;
	*block_size = FS_Constants::BYTES_PER_SECTOR;
}

//always return true
//if load_eject = true, if(start) load(), else eject();
bool MSC_Interface::handle_msc_start_stop(uint8_t power_condition, bool start, bool load_eject) {
	(void) power_condition; //example ignores this

	//if the host wants us to no longer be accessible
	//make us inaccessible
	if(load_eject && !start) accessible = false;

	return true;
}

//for custom SCSI commands that aren't the standard ones
//return -1 if it was an illegal request and can't be handled
int32_t MSC_Interface::handle_msc_scsi_custom(uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
	(void) buffer;
	(void) bufsize;
	(void) scsi_cmd;

	//don't handle any non-standard command
	tud_msc_set_sense(msc_channel.lun_no, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

	return -1;
}

//return true if the file system is writable, else false
bool MSC_Interface::handle_msc_is_writable() {
	//handle r/w based on individual files
	return true;
}

//handle writing to blocks
//return number of written bytes or -1 if error
int32_t MSC_Interface::handle_msc_write10(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
	//handling offsets in memory is *very* annoying, so just gonna error out if we have a non-zero offset
	//shouldn't run this given our buffer and sector size configurations
	if(offset) return -1;

	//and a quick exit if we have nothing to copy
	if(bufsize == 0) return 0;

	//compute how many sectors we need to read, sanity check that we don't go outta bounds of memory
	//and maintain a pointer to where in the buffer we have to write the particular contents of the buffer
	const size_t num_write_sectors = (bufsize + FS_Constants::BYTES_PER_SECTOR - 1)/FS_Constants::BYTES_PER_SECTOR;
	if(lba + num_write_sectors > FS_Constants::DATA_SECTOR_END) return -1;
	uint32_t buffer_offset = 0;
	size_t remaining_bytes = bufsize;

	//handle each sector we gotta read
	for(uint32_t i = lba; i < lba + num_write_sectors; i++) {
		//wrap a portion of the input buffer in a span
		//slice the input buffer based off the buffer offset
		std::span<uint8_t, std::dynamic_extent> write_sector(	&buffer[buffer_offset],									//start writing the bytes from the offset
																min(FS_Constants::BYTES_PER_SECTOR, remaining_bytes));	//and write the lesser of one sector, or remaining bytes
		bool write_success = false;

		//TODO: dispatch the sector read request according to the block address
		if		(i < FS_Constants::BOOT_SECTOR_END) write_success = true;	//silently succeed, but don't allow editing of the boot sector
		else if	(i < FS_Constants::FAT1_SECTOR_END) write_success = true;	//silently succeed, but don't allow editing of the FAT1 sector
		else if	(i < FS_Constants::FAT2_SECTOR_END) write_success = true;	//silently succeed, but don't allow editing of the FAT2 sector
		else if	(i < FS_Constants::ROOT_SECTOR_END) write_success = true;	//silently succeed, but don't allow editing of the ROOT sector
		else if (i < FS_Constants::DATA_SECTOR_END)	write_success = data_sector.write(i - FS_Constants::DATA_SECTOR_START, write_sector); //allow editing of data sector

		if(!write_success) return -1;

		//modify our buffer offset and remaining buffer size
		buffer_offset += FS_Constants::BYTES_PER_SECTOR;
		remaining_bytes -= FS_Constants::BYTES_PER_SECTOR;
	}

	//
	return (int32_t) bufsize;
}

//and finally, handle reading from blocks
//return number of bytes copied or -1 if error
int32_t MSC_Interface::handle_msc_read10(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
	//handling offsets in memory is *very* annoying, so just gonna error out if we have a non-zero offset
	//shouldn't run this given our buffer and sector size configurations
	if(offset) return -1;

	//and a quick exit if we have nothing to copy
	if(bufsize == 0) return 0;

	//compute how many sectors we need to read, sanity check that we don't go outta bounds of memory
	//and maintain a pointer to where in the buffer we have to write the particular contents of the buffer
	const size_t num_read_sectors = (bufsize + FS_Constants::BYTES_PER_SECTOR - 1)/FS_Constants::BYTES_PER_SECTOR;
	if(lba + num_read_sectors > FS_Constants::DATA_SECTOR_END) return -1;
	uint32_t buffer_offset = 0;
	size_t remaining_bytes = bufsize;

	//handle each sector we gotta read
	for(uint32_t i = lba; i < lba + num_read_sectors; i++) {
		//wrap a portion of the input buffer in a span
		//slice the input buffer based off the buffer offset
		std::span<uint8_t, std::dynamic_extent> read_data(	&((uint8_t*)buffer)[buffer_offset],						//start writing the bytes from the offset
															min(FS_Constants::BYTES_PER_SECTOR, remaining_bytes));	//and write the lesser of one sector, or remaining bytes
		bool read_success = false;

		//dispatch the sector read request according to the block address
		if		(i < FS_Constants::BOOT_SECTOR_END) read_success = boot_sector.read(i - FS_Constants::BOOT_SECTOR_START, read_data);	//BOOT sector
		else if	(i < FS_Constants::FAT1_SECTOR_END) read_success = fat_table.read(i - FS_Constants::FAT1_SECTOR_START, read_data);	//FAT1 sectors
		else if	(i < FS_Constants::FAT2_SECTOR_END) read_success = fat_table.read(i - FS_Constants::FAT2_SECTOR_START, read_data);	//FAT2 sectors
		else if	(i < FS_Constants::ROOT_SECTOR_END)	read_success = root_sector.read(i - FS_Constants::ROOT_SECTOR_START, read_data); 	//ROOT sectors
		else if (i < FS_Constants::DATA_SECTOR_END)	read_success = data_sector.read(i - FS_Constants::DATA_SECTOR_START, read_data);	//DATA sectors

		//if there was an issue with the read, just return
		if(!read_success) return -1;

		//modify our buffer offset and remaining buffer size
		buffer_offset += FS_Constants::BYTES_PER_SECTOR;
		remaining_bytes -= FS_Constants::BYTES_PER_SECTOR;
	}

	//if we got here, all our reads are successful
	return (int32_t) bufsize;
}

//======================================= TINYUSB HANDLERS ===================================
//all of these will basically forward to their corresponding instance
uint32_t tud_msc_inquiry2_cb(uint8_t lun, scsi_inquiry_resp_t *inquiry_resp, uint32_t bufsize) {
	if(lun == MSC_Interface::MSC_CHANNEL.lun_no) {
		if(MSC_Interface::MSC_CHANNEL.msc_if)
			return MSC_Interface::MSC_CHANNEL.msc_if->handle_msc_inquiry(inquiry_resp, bufsize);
	}
	return -1;	//default
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
	if(lun == MSC_Interface::MSC_CHANNEL.lun_no) {
		if(MSC_Interface::MSC_CHANNEL.msc_if)
			return MSC_Interface::MSC_CHANNEL.msc_if->handle_msc_ready();
	}
	return false; //default
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
	if(lun == MSC_Interface::MSC_CHANNEL.lun_no) {
		if(MSC_Interface::MSC_CHANNEL.msc_if)
			return MSC_Interface::MSC_CHANNEL.msc_if->handle_msc_capacity(block_count, block_size);
	}
	return; //default
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
	if(lun == MSC_Interface::MSC_CHANNEL.lun_no) {
		if(MSC_Interface::MSC_CHANNEL.msc_if)
			return MSC_Interface::MSC_CHANNEL.msc_if->handle_msc_start_stop(power_condition, start, load_eject);
	}
	return false; //default
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
	if(lun == MSC_Interface::MSC_CHANNEL.lun_no) {
		if(MSC_Interface::MSC_CHANNEL.msc_if)
			return MSC_Interface::MSC_CHANNEL.msc_if->handle_msc_scsi_custom(scsi_cmd, buffer, bufsize);
	}
	return -1; //default
}

bool tud_msc_is_writable_cb(uint8_t lun) {
	if(lun == MSC_Interface::MSC_CHANNEL.lun_no) {
		if(MSC_Interface::MSC_CHANNEL.msc_if)
			return MSC_Interface::MSC_CHANNEL.msc_if->handle_msc_is_writable();
	}
	return false; //default
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
	if(lun == MSC_Interface::MSC_CHANNEL.lun_no) {
		if(MSC_Interface::MSC_CHANNEL.msc_if)
			return MSC_Interface::MSC_CHANNEL.msc_if->handle_msc_write10(lba, offset, buffer, bufsize);
	}
	return -1; //default
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
	if(lun == MSC_Interface::MSC_CHANNEL.lun_no) {
		if(MSC_Interface::MSC_CHANNEL.msc_if)
			return MSC_Interface::MSC_CHANNEL.msc_if->handle_msc_read10(lba, offset, buffer, bufsize);
	}
	return -1; //default
}





