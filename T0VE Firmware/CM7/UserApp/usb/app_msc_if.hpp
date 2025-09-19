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

#include "app_usb_if.hpp"

//forward declaring MSC_File class for MSC_Interface
class MSC_File;

class MSC_Interface {
public:

	//================= TYPEDEFS + CONSTANTS ===================
	//want static allocation, so determine once how many files we want to support
	static const size_t MAX_NUM_FILES = 8;

	//way to describe a particular MSC interface
	struct MSC_Interface_Channel_t {
		USB_Interface& usb;		//We have instantiated a particular USB class
		const size_t usb_if_no;
		const size_t msc_if_no;
		MSC_Interface* msc_if;	//MSC_Interface class with file information we'd like to present
	};

	static const MSC_Interface_Channel_t MSC_CHANNEL;

	//================== CONSTRUCTOR =================
	MSC_Interface(MSC_Interface_Channel_t& channel);

	//delete copy constructor and assignment operator
	MSC_Interface(const MSC_Interface& other) = delete;
	void operator=(const MSC_Interface& other) = delete;

	//specialized destructor that detaches all the files
	~MSC_Interface();
	//================= PUBLIC FUNCTIONS =============

	//`init`s the upstream hardware if required
	void init();

	//request/allow the upstream USB peripheral to connect
	//useful if we need to suspend the USB port for some reason
	void connect_request();

	//request the upstream USB peripheral to disconnect
	//NOTE: if any other USB classes need USB, the interface will not disconnect!
	//it will only disconnect if ALL downstream interfaces request the USB peripheral to disconnect
	void disconnect_request();

	//make this particular file show up in the MSC interface
	//MSC will take care of building the FAT table and coordinating file reads/writes
	void attach_file(MSC_File& _file);

	//and have an interface that removes a file from the MSC interface
	void detach_file(MSC_File& _file);

private:
	//TODO: lot of private functions that are forwarded from TinyUSB
	//these will handle construction of our FAT table and size/descriptor requests

	//reference to our hardwawre/tinyUSB instance
	MSC_Interface_Channel_t& msc_channel;

	//A list of pointers to "files" that we'd like to represent
	std::array<MSC_File*, MAX_NUM_FILES> msc_files = {nullptr};
};


/*
 * MSC_File
 *
 * In the context of embedded, if I wanna expose a mass storage interface,
 * I typically wanna expose a chunk of memory as a file that the computer can read
 * However, I can't directly dump the file contents onto USB--there typically is a file system that lives on top of it
 * As such, the idea is that the `MSC_Interface` handles the actual file system, presenting all of our chunks of memory as readable files
 * but the actual memory is wrapped in these little `MSC_File` classes
 */

class MSC_File {
public:

	//give the MSC interface special privileges in invoking callbacks
	//and also ability to associate itself with with the particular file
	friend class MSC_Interface;

	//fixed size containers for filename
	//makes static allocation easier
	static const size_t FILENAME_MAX_LENGTH = 32;

	//================= CONSTRUCTOR ================
	MSC_File(	std::span<uint8_t, std::dynamic_extent> _file_contents,
				std::array<char, FILENAME_MAX_LENGTH> _file_name,
				bool _readonly = false):
		file_contents(_file_contents), file_name(_file_name), readonly(_readonly)
	{}

	//delete copy constructor and assignment operator
	MSC_File(const MSC_File& other) = delete;
	void operator=(const MSC_File& other) = delete;

	//and have a specialized destructor that removes the file from the MSC interface
	//in case we had attached this file to an MSC interface
	~MSC_File() {	if(msc_if) msc_if->detach_file(*this);	}

	//and registering some callback functions for when we write to/read from files
	//called before, after files are read, written respectively
	//useful for attaching mutexes or preparing data
	void file_read_start_cb(Callback_Function<> cb) 	{ 	read_start = cb; 	}
	void file_read_finish_cb(Callback_Function<> cb) 	{ 	read_finish = cb; 	}
	void file_write_start_cb(Callback_Function<> cb) 	{ 	write_start = cb;	}
	void file_write_finish_cb(Callback_Function<> cb)	{ 	write_finish = cb;	}

private:
	//file details
	const std::span<uint8_t, std::dynamic_extent> file_contents;
	const std::array<char, FILENAME_MAX_LENGTH> file_name;
	const bool readonly;

	//MSC interface --> creating a bidirectional link in case we destroy the instance
	//allows the instance to detach itself from the MSC interface
	MSC_Interface* msc_if = nullptr;

	//callback functions for file reading/writing
	Callback_Function<> read_start;
	Callback_Function<> read_finish;
	Callback_Function<> write_start;
	Callback_Function<> write_finish;
};
