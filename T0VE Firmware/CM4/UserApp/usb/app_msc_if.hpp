/*
 * app_msc_interface.hpp
 *
 *  Created on: Sep 19, 2025
 *      Author: govis
 *
 *  This is a little wrapper to the TinyUSB MSC interface functions
 *  This class will implement all MSC-related callbacks directly
 *  And with the help of `MSC_File`, present sections of device memory as files that can be read and
 *  (optionally) be written to
 */

#pragma once

#include "app_msc_constants.hpp"
#include "tusb.h"
#include "app_usb_if.hpp"
#include "app_msc_file.hpp"
#include "app_msc_boot_sector.hpp"
#include "app_msc_root_sector.hpp"
#include "app_msc_fat_table.hpp"
#include "app_msc_data_sector.hpp"

#include "app_string.hpp"
#include "app_vector.hpp"	//growable list of files

class MSC_Interface {
public:

	//================= TYPEDEFS + CONSTANTS ===================

	//way to describe a particular MSC interface
	struct MSC_Interface_Channel_t {
		const size_t lun_no;	//Logical unit no. if we have more than 1 MSC class
		MSC_Interface* msc_if;	//MSC_Interface class with file information we'd like to present
	};

	static MSC_Interface_Channel_t MSC_CHANNEL;

	//================== CONSTRUCTOR =================
	MSC_Interface(USB_Interface& _usb_if, MSC_Interface_Channel_t& channel);

	//delete copy constructor and assignment operator
	MSC_Interface(const MSC_Interface& other) = delete;
	void operator=(const MSC_Interface& other) = delete;

	//================= PUBLIC FUNCTIONS =============

	//`init`s the upstream hardware if required
	void init();

	//notify the host unit that contents of one of our files has changed
	//do so by issuing the appropriate SCSI commands on the next `ready` request
	void notify_file_change();

	//request/allow the upstream USB peripheral to connect
	//useful if we need to suspend the USB port for some reason
	//TODO: add some SCSI logic in here to allow connections
	void connect_request();

	//request the upstream USB peripheral to disconnect
	//NOTE: if any other USB classes need USB, the interface will not disconnect!
	//it will only disconnect if ALL downstream interfaces request the USB peripheral to disconnect
	//TODO: add some SCSI logic in here to disallow connections
	void disconnect_request();

	//volume name; scsi vid, pid, rev
	//no need to tweak USB interface string descriptor - already a ton of descriptors to expose
	void set_string_fields(	const App_String<11, ' '>	_vol_name,
							const App_String<8, ' '>  _scsi_vid,
							const App_String<16, ' '>  _scsi_pid,
							const App_String<4, ' '>  _scsi_rev	);

	//make this particular file show up in the MSC interface
	//MSC will take care of building the FAT table and coordinating file reads/writes
	void attach_file(MSC_File& _file);

	//and have an interface that removes a file from the MSC interface
	void detach_file(MSC_File& _file);


	//###### TINYUSB FORWARDED FUNCTIONS ######
	//annoyingly have to expose these publicly, since TinyUSB has fixed callbacks
	//VID, PID, REV
	uint32_t handle_msc_inquiry(scsi_inquiry_resp_t *inquiry_resp, uint32_t bufsize);

	//return true if host can read/write to the device; return false if ejected
	bool handle_msc_ready();

	//use constants --> block size = sector size; block count = total #sectors
	void handle_msc_capacity(uint32_t* block_count, uint16_t* block_size);

	//always return true
	//if load_eject = true, if(start) load(), else eject();
	bool handle_msc_start_stop(uint8_t power_condition, bool start, bool load_eject);

	//for custom SCSI commands that aren't the standard ones
	//return -1 if it was an illegal request and can't be handled
	int32_t handle_msc_scsi_custom(uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize);

	//return true if the file system is writable, else false
	bool handle_msc_is_writable();

	//handle writing to blocks
	//return number of written bytes or -1 if error
	int32_t handle_msc_write10(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize);

	//and finally, handle reading from blocks
	//return number of bytes copied or -1 if error
	int32_t handle_msc_read10(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize);

	//#########################################
private:
	//call this to regenerate our emulated FAT sectors
	void regenerate();

	//reference to our hardwawre/tinyUSB instance
	USB_Interface& usb_if;
	MSC_Interface_Channel_t& msc_channel;

    //A growable, bounded list of files we'd like to represent
    App_Vector<MSC_File, FS_Constants::MAX_NUM_FILES> msc_files;

	//Have some special classes that manage the creation/handling of special sector requests
	Boot_Sector boot_sector;
	Root_Sector root_sector;
	FAT16_Table fat_table;
	Data_Sector data_sector;

	//a little bool flag to hold if we'd like to expose we're accessible
	bool accessible = false;

	//and a bool flag that we assert when we want to tell the host one of our files changed
	bool file_changed = false;

	//some strings useful for the SCSI interface + FATFS emulator
	App_String<8, ' '> scsi_vid = "Ayo Elec";
	App_String<16, ' '> scsi_pid = "Processor Card";
	App_String<4, ' '> scsi_rev = "A.15";
	App_String<11, ' '> vol_name = "Node Memory";
};


