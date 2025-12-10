/*
 * app_basic_file.cpp
 *
 *  Created on: Dec 8, 2025
 *      Author: govis
 */


#include "app_basic_file.hpp"

//non-trivial constructor
Basic_File::Basic_File(	std::span<uint8_t, std::dynamic_extent> _file_contents,
					App_String<Basic_File::FILENAME_MAX_LENGTH> _file_name,
					bool _readonly,
					Mutex* _file_mutex):
	file_contents(_file_contents), file_name(_file_name), readonly(_readonly), file_mutex(_file_mutex)
{}

//file validity checking --> check span to see if it was nontrivially initialized
bool Basic_File::is_valid() { return !(file_contents.empty() || (file_contents.data() == nullptr)); }

//comparison operator override
bool Basic_File::operator==(const Basic_File& other) const {
	if(this->file_contents.size() != other.file_contents.size()) return false;
	if(this->file_contents.data() != other.file_contents.data()) return false;
	return true;
}

//########## READ FUNCTION ##########
size_t Basic_File::read(size_t byte_offset, std::span<uint8_t, std::dynamic_extent> copy_dest) {
	//sanity check our starting address; return if outta range
	if(byte_offset >= file_contents.size()) return 0;

	//we'll be reading some bytes, try to acquire the mutex if it exists
	if(file_mutex)
		if(!file_mutex->TRY_LOCK()) return 0;	//mutex acquisition failed, abort copy

	//perform our actual copy
	size_t req_size = copy_dest.size();
	size_t rem_size = file_contents.size() - byte_offset;
	size_t copy_size = min(req_size, rem_size);
	std::copy(file_contents.begin() + byte_offset, file_contents.begin() + byte_offset + copy_size, copy_dest.begin());

	//and if we had a mutex, unlock the mutex
	if(file_mutex) file_mutex->UNLOCK();

	//return number of bytes we copied
	return copy_size;
}

size_t Basic_File::write(size_t byte_offset, std::span<uint8_t, std::dynamic_extent> copy_src) {
	//sanity check our starting address; copy no bytes if outta range
	//and if the file is readonly, copy no bytes
	if(byte_offset >= file_contents.size()) return 0;
	if(readonly) return 0;

	//we'll be writing some bytes, try to acquire the mutex if it exists
	if(file_mutex)
		if(!file_mutex->TRY_LOCK()) return 0;	//mutex acquisition failed, abort copy

	//perform our actual copy
	size_t req_size = copy_src.size();
	size_t rem_size = file_contents.size() - byte_offset;
	size_t copy_size = min(req_size, rem_size);
	std::copy(copy_src.begin(), copy_src.begin() + copy_size, file_contents.begin() + byte_offset);

	//and if we had a mutex, unlock the mutex
	if(file_mutex) file_mutex->UNLOCK();

	//return number of bytes we copied
	return copy_size;
}



