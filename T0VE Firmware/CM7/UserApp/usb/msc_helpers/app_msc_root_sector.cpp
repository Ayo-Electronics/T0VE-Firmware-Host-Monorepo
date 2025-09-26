/*
 * app_msc_root_sector.cpp
 *
 *  Created on: Sep 23, 2025
 *      Author: govis
 */

#include "app_msc_root_sector.hpp"

void Root_Sector::mk(	std::array<uint8_t, 11> volume_label,
						std::array<MSC_File, FS_Constants::MAX_NUM_FILES>& files,
						FAT16_Table::File_Indices_t& cluster_indices)
{
	//leverage a lotta the helper functions
	//start by making a volume label
	Root_Data_Entry_t vol_entry = mk_root_entry_volname(volume_label);

	//then go through all the files and make root entries for valid ones
	std::array<Root_File_Buffer_t, FS_Constants::MAX_NUM_FILES> file_buffers = {0};	//actual storage for each root file entry
	std::array<Root_File_Entry_t, FS_Constants::MAX_NUM_FILES> file_entries = {};	//pointers into the storage

	//make entries for all the files
	for(size_t i = 0; i < FS_Constants::MAX_NUM_FILES; i++) {
		//get the file and the start cluster associated with that file
		auto& file = files[i];
		auto cluster = cluster_indices.start_indices[i];
		auto& buffer = file_buffers[i];

		//build an entry for the file
		//automatically checks if the file is valid
		file_entries[i] = mk_root_entry_file(file, cluster, buffer);
	}

	//once we have our volume entries and file entries, update our root sectors
	//NOTE: our entries refer to our buffers; should still be in scope through this function
	update_root_sector(vol_entry, file_entries);
}

bool Root_Sector::read(size_t sector_offset, std::span<uint8_t, std::dynamic_extent> sec) {
	//default our output to 0
	std::fill(sec.begin(), sec.end(), 0);

	//figure out the start address we wanna copy from based on sector offset
	size_t address_offset = sector_offset * FS_Constants::BYTES_PER_SECTOR;

	//and if that address offset is less than our size
	//copy the root sector starting from the particular address into our output
	//but make sure not to go beyond the end of the root sector
	if(address_offset < root_sector.size()) {
		//compute where we'd like to end our copy--lesser of size of root sector and output buffer size + offset
		size_t req_size = sec.size();
		size_t rem_size = root_sector.size() - address_offset;
		size_t copy_size = min(req_size, rem_size);

		std::copy(	root_sector.begin() + address_offset,
					root_sector.begin() + address_offset + copy_size,
					sec.begin()	);
	}

	//return true--will always be able to provide something valid
	return true;
}

//============================================ PRIVATE HELPER FUNCTIONS ==========================================

Root_Sector::Root_Data_Entry_t Root_Sector::mk_root_entry_volname(std::array<uint8_t, 11> vol_name) {
	//placeholder
	//indicate that we're a volume label (0x08 in spot 11)
	//leaving timestamps, clusters, and size as 0
	Root_Data_Entry_t entry = {0};
	entry[11] = 0x08;

	//otherwise just copy our volume name into the beginning of the entry
	std::copy(vol_name.begin(), vol_name.end(), entry.begin());

	return entry; //and return our entry
}

//TODO: fix
Root_Sector::Root_File_Entry_t Root_Sector::mk_root_entry_file(MSC_File& file, uint16_t cluster, Root_File_Buffer_t& file_buffer) {
	//early exit if file is invalid; return a zero-sized `span`
	if(!file.is_valid()) return std::span<uint8_t, std::dynamic_extent>(file_buffer.data(), 0);

	//##### SFN ENTRY #####
	//Build the SFN directory entry (32 bytes), then prepend fixed-count LFN entries
	Root_Data_Entry_t short_entry = {0};

	// Copy in the 8.3 name
	auto sfn = file.get_short_name();
	std::copy(sfn.name.begin(), sfn.name.end(), short_entry.begin() + 0);
	std::copy(sfn.ext.begin(),  sfn.ext.end(), short_entry.begin() + 8);

	// Attributes: Archive always; add ReadOnly if applicable
	uint8_t attr = 0x20 | 0x04; // Archive + System (discourage the OS from messing with this file location too much)
	if(file.get_read_only()) attr |= 0x01; // Read-only
	Regmap_Field(11, 0, 8,  false, short_entry) = attr;

	// Timestamps and reserved fields left as 0 for now

	// First cluster high (FAT16) = 0 by default
	// First cluster low
	Regmap_Field(26, 0, 16, false, short_entry) = static_cast<uint16_t>(cluster);

	// File size (bytes)
	Regmap_Field(28, 0, 32, false, short_entry) = static_cast<uint32_t>(file.get_file_size());

	//drop the actual SFN entry at the end of our buffer
	//build it back to front since LFNs come before the file entry
	//but start with the short entry
	size_t entry_byte_index = file_buffer.size() - short_entry.size();
	std::copy(short_entry.begin(), short_entry.end(), file_buffer.begin() + entry_byte_index);

	//##### BUILDING LFN ENTRIES #####
	//grab our long file name, and compute how many LFN entries it requires to make
	auto long_name8 = file.get_file_name();
	const size_t actual_len = long_name8.size();
	const size_t n = (actual_len + LFN_CHARS_PER_ENTRY - 1) / LFN_CHARS_PER_ENTRY;	//how many LFN entries we need, round up

	//build our LFN entries successively
	size_t name_copy_offset = 0;
	size_t name_remaining = actual_len;
	for (size_t k = 1; k <= n; k++) {
		//create a std::array to hold the slice of the filename characters
		std::array<uint8_t, LFN_CHARS_PER_ENTRY> name_slice;
		name_slice.fill(0xFF);

		//check if we need to add a null character, and if this is the last element of the sequence
		//similar, but not exactly the same checks
		bool is_last_lfn = false;
		if(name_remaining <= LFN_CHARS_PER_ENTRY) is_last_lfn = true;				//if we can fit the rest of the name into this buffer batch
		if(name_remaining < LFN_CHARS_PER_ENTRY) name_slice[name_remaining] = 0;	//and if will have space for our null terminator

		//compute how many chars we need to copy and perform the copy
		//update our pointers afterward
		size_t to_copy = min(name_remaining, LFN_CHARS_PER_ENTRY);
		std::copy(	long_name8.begin() + name_copy_offset,
					long_name8.begin() + name_copy_offset + to_copy,
					name_slice.begin());
		name_remaining -= to_copy;
		name_copy_offset += to_copy;

		// build one LFN entry (back to front)
		auto lfn_k = mk_root_entry_lfn(name_slice, sfn.checksum, k, is_last_lfn);

		//and copy over the lfn entry; move the entry_byte_index back accordingly
		entry_byte_index -= lfn_k.size();
		std::copy(lfn_k.begin(), lfn_k.end(), file_buffer.begin() + entry_byte_index);
	}

	//at this point, `entry_byte_index` points to the start index of the buffer
	//return a span that points to the buffer and is the correct size
	return std::span<uint8_t, std::dynamic_extent>(file_buffer.begin() + entry_byte_index, file_buffer.end());
}

Root_Sector::Root_LFN_Entry_t Root_Sector::mk_root_entry_lfn(	std::span<uint8_t, LFN_CHARS_PER_ENTRY> lfn_chars,
															   	uint8_t sfname_checksum,
																size_t lfn_index, bool last)
{
	//create our output and fill with 0xFF --> what our UTF8s default to
	Root_LFN_Entry_t entry;
    entry.fill(0xFF);

    //### SEQUENCE NUMBER
    //place the index of our sequence right at the beginning of our entry
    //0x40 indicates that this entry is the last of the LFN
    uint8_t seq = static_cast<uint8_t>(lfn_index & 0x3F);
    if (last) seq |= 0x40; // mark as last entry if necessary
    Regmap_Field(0, 0, 8, false, entry) = seq;

    //#### LFN CHARACTER LOCATIONS
    std::array<Regmap_Field, LFN_CHARS_PER_ENTRY> char_positions = {
    		Regmap_Field( 1, 0, 16, false, entry),	//5 characters at beginning of LFN entry
            Regmap_Field( 3, 0, 16, false, entry),
            Regmap_Field( 5, 0, 16, false, entry),
            Regmap_Field( 7, 0, 16, false, entry),
            Regmap_Field( 9, 0, 16, false, entry),

            Regmap_Field(14, 0, 16, false, entry),	//6 characters in middle of LFN entry
            Regmap_Field(16, 0, 16, false, entry),
            Regmap_Field(18, 0, 16, false, entry),
            Regmap_Field(20, 0, 16, false, entry),
            Regmap_Field(22, 0, 16, false, entry),
            Regmap_Field(24, 0, 16, false, entry),

            Regmap_Field(28, 0, 16, false, entry),	//2 characters at end of LFN entry
            Regmap_Field(30, 0, 16, false, entry),
        };

    //put the actual characters into the correct spot
    //ASCII to UTF-16 conversion just requires zero extension
	for (size_t i = 0; i < LFN_CHARS_PER_ENTRY; i++) {
		if(lfn_chars[i] == 0xFF) char_positions[i] = 0xFFFF;			//have to stuff the high bytes too
		else char_positions[i] = static_cast<uint16_t>(lfn_chars[i]);	//otherwise, can just stuff the low bytes
	}

    //###### ATTRIBUTE, TYPE, CHECKSUM, ZERO FIELD
    Regmap_Field(11, 0, 8, false, entry) = 0x0F;             // attr = LFN
    Regmap_Field(12, 0, 8, false, entry) = 0x00;             // type
    Regmap_Field(13, 0, 8, false, entry) = sfname_checksum;  // checksum
    Regmap_Field(26, 0, 16, false, entry) = 0x0000;

    return entry;
}

void Root_Sector::update_root_sector(	Root_Data_Entry_t& volname_entry,
										std::array<Root_File_Entry_t, FS_Constants::MAX_NUM_FILES>& file_entries)
{
	//zero-initialize our root entry
	root_sector.fill(0);

	//initialize an index into our root entry table
	size_t root_index = 0;

	//put our volume name right at the beginning
	std::copy(volname_entry.begin(), volname_entry.end(), root_sector.begin());
	root_index += volname_entry.size();

	//then iteratively copy over our file entries into our root sector
	for(auto& file : file_entries) {
		std::copy(file.begin(), file.end(), root_sector.begin() + root_index);
		root_index += file.size();
	}
}

