/*
 * app_basic_file.hpp
 *
 *  Created on: Dec 8, 2025
 *      Author: govis
 *
 *  Alternative to the MSC File class;
 */

#pragma once

#include "app_proctypes.hpp"
#include "app_utils.hpp"
#include "app_threading.hpp"
#include "app_string.hpp"	//for filename

class Basic_File {
public:
	//CHANGE TO INCREASE FILENAME LENGTH
	static const size_t FILENAME_MAX_LENGTH = 32;

	//================= CONSTRUCTOR ================
	//trivial constructor so we can do array initialization
	Basic_File() {}

	//Virtual destructor if we extend this class
	virtual ~Basic_File() {}

	//and more complete constructor that actually initializes things
	Basic_File(	std::span<uint8_t, std::dynamic_extent> _file_contents,
				App_String<FILENAME_MAX_LENGTH> _file_name,
				bool _readonly = false,
				Mutex* _file_mutex = nullptr);

	//========== PUBLIC FUNCTIONS ==========
	//check if the file is valid--pointing to a non-null memory address with nonzero size
	//NOTE: data pointer *might* not be `nullptr`. Though if its valid, it will definitely NOT be nullptr
	bool is_valid();

	//overriding equality comparison operator
	//if the files point to the same memory and have the same length, then they're equal
	//not doing the default `span` comparison, since that checks the underlying array elements for equality
	//and for a multi-megabyte file, this may take a while
	bool operator==(const Basic_File& other) const;

	//### FILE READ/WRITE ###
	//read the file with the specified length and offset
	//copies the bytes into the destination buffer (also uses the size of this to determine the # of bytes to copy)
	//returns actual number of bytes copied
	size_t read(size_t byte_offset, std::span<uint8_t, std::dynamic_extent> copy_dest);

	//write the file with the specificed length and offset
	//copies the bytes from the source buffer (also uses the size of this to determin the # of bytes to copy)
	//tries to claim mutex if it's non-null, escapes if it can't
	//returns actual # of bytes copied
	size_t write(size_t byte_offset, std::span<uint8_t, std::dynamic_extent> copy_src);

	//### ACCESSORS ###
	inline App_String<FILENAME_MAX_LENGTH> get_file_name() { return file_name; }
	inline bool get_read_only() { return readonly; }
	inline size_t get_file_size() { return file_contents.size(); }

private:
	//file details; not marking const to allow for assignment
	std::span<uint8_t, std::dynamic_extent> file_contents;
	App_String<FILENAME_MAX_LENGTH> file_name;	//fix max filename size to our max length
	bool readonly = true;										//default read-only

	//and hold a pointer to a mutex for the file
	//since our file is likely a shared system resource, makes sure we access atomically
	Mutex* file_mutex;
};
