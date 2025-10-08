/*
 * app_msc_file.hpp
 *
 *  Created on: Sep 23, 2025
 *      Author: govis
 */

#pragma once

#include "app_msc_constants.hpp"
#include "app_proctypes.hpp"
#include "app_utils.hpp"
#include "app_threading.hpp"
#include "app_string.hpp"	//for filename

//forward declaring MSC_Interface class for friending
class MSC_Interface;


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
	//and a struct for 8.3 filenames
	struct FName_8d3_t {
		std::array<uint8_t, 8> name;
		std::array<uint8_t, 3> ext;
		uint8_t checksum;
	};

	//================= CONSTRUCTOR ================
	//trivial constructor so we can do array initialization
	MSC_File() {}

	//and more complete constructor that actually initializes things
	MSC_File(	std::span<uint8_t, std::dynamic_extent> _file_contents,
				App_String<FS_Constants::FILENAME_MAX_LENGTH> _file_name,
				bool _readonly = false,
				Mutex* _file_mutex = nullptr);

	//use default copy constructor and assignment operators
	//should be fine philosophically, since an MSC file is just a thin wrapper around existing memory
	//NOTE: since the 8.3 filename is based around the address of the current instance,
	//		the default copy constructor/assignment operator copy in an INCORRECT 8.3 name
	//		that's why I regenerate the 8.3 name in the getter function itself
	//		otherwise I'd have to use a non-default copy constructor/assignment operator which is cumbersome

	//========== PUBLIC FUNCTIONS ==========
	//check if the file is valid--pointing to a non-null memory address with nonzero size
	//NOTE: data pointer *might* not be `nullptr`. Though if its valid, it will definitely NOT be nullptr
	bool is_valid();

	//overriding equality comparison operator
	//if the files point to the same memory and have the same length, then they're equal
	//not doing the default `span` comparison, since that checks the underlying array elements for equality
	//and for a multi-megabyte file, this may take a while
	bool operator==(const MSC_File& other) const;

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
	inline std::span<uint8_t, std::dynamic_extent> get_file_name() { return file_name.span(); } //use built-in conversion function
	inline bool get_read_only() { return readonly; }
	inline FName_8d3_t get_short_name() { mk_8p3(); return short_name; } //make the 8.3 name on demand; avoids constructor complications
	inline size_t get_file_size() { return file_contents.size(); }

private:
	//file details; not marking const to allow for assignment
	std::span<uint8_t, std::dynamic_extent> file_contents;
	App_String<FS_Constants::FILENAME_MAX_LENGTH> file_name;	//fix max filename size to our max length
	bool readonly = true;										//default read-only

	//8.3 name, default to 0
	FName_8d3_t short_name = {0};

	//quick functions to make an 8.3 filename
	void mk_8p3();

	//and hold a pointer to a mutex for the file
	//since our file is likely a shared system resource, makes sure we access atomically
	Mutex* file_mutex;
};
