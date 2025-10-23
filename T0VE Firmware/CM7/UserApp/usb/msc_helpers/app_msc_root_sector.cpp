/*
 * app_msc_root_sector.cpp
 *
 *  Created on: Sep 23, 2025
 *      Author: govis
 */

#include "app_msc_root_sector.hpp"

void Root_Sector::mk(	App_String<11, ' '>& volume_label,
						std::span<MSC_File> files,
						FAT16_Table::File_Indices_t& cluster_indices)
{
	//leverage a lotta the helper functions
	//start by making a volume label
	Root_Data_Entry_t vol_entry = mk_root_entry_volname(volume_label);

	//then go through all the files and make root entries for valid ones
	App_Vector<Root_File_Entry_t, FS_Constants::MAX_NUM_FILES> file_entries = {};	//growable container of growable containers

	//make entries for all the files
	for(size_t i = 0; i < files.size(); i++) {
		//get the file and the start cluster associated with that file
		auto& file = files[i];
		auto start_cluster = cluster_indices.start_indices[i];

		//build a new entry and add it to the end of our list of entries
		file_entries.push_back(mk_root_entry_file(file, start_cluster));
	}

	//once we have our volume entries and file entries, update our root sectors
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

Root_Sector::Root_Data_Entry_t Root_Sector::mk_root_entry_volname(App_String<11, ' '>& vol_name) {
	//placeholder
	//indicate that we're a volume label (0x08 in spot 11)
	//leaving timestamps, clusters, and size as 0
	Root_Data_Entry_t entry = {0};
	entry[11] = 0x08;

	//otherwise just copy our volume name into the beginning of the entry
	std::copy(vol_name.array().begin(), vol_name.array().end(), entry.begin());

	return entry; //and return our entry
}

Root_Sector::Root_File_Entry_t Root_Sector::mk_root_entry_file(MSC_File& file, uint16_t cluster) {
	//output temporary
	Root_Sector::Root_File_Entry_t file_entry = {};

	//##### BUILDING LFN ENTRIES #####
	//start by building a padded, terminated filename string
	App_Vector<uint8_t, FS_Constants::FILENAME_MAX_LENGTH> padded_filename = {};
	auto fname = file.get_file_name();
	const size_t n_lfn = (fname.size() + LFN_CHARS_PER_ENTRY - 1) / LFN_CHARS_PER_ENTRY;	//how many LFN entries we need, round up
	const size_t desired_length = n_lfn * LFN_CHARS_PER_ENTRY;								//compute how long we'd like our padded filename to be
	padded_filename.push_n_back(fname.span());												//push our filename into our growable buffer
	if(padded_filename.size() < desired_length) padded_filename.push_back(0);				//push a null terminator to our string if we have space
	while(padded_filename.size() < desired_length) padded_filename.push_back(0xFF);			//add padding until filename is desired size

	//section the padded filename into LFN-entry-sized chunks; store in an app-vector
	using LFN_Name_Chunk_t = std::array<uint8_t, LFN_CHARS_PER_ENTRY>;
	App_Vector<LFN_Name_Chunk_t, REQUIRED_LFN_ENTRIES> padded_filename_sections = {};
	for(size_t i = 0; i < n_lfn; i++) {
		LFN_Name_Chunk_t name_chunk;

		//calculate some offsets, and copy into the temporary
		size_t start_offset = i * LFN_CHARS_PER_ENTRY;
		size_t end_offset = start_offset + LFN_CHARS_PER_ENTRY;
		std::copy(padded_filename.begin() + start_offset, padded_filename.begin() + end_offset, name_chunk.begin());

		//push the temporary into our vector
		padded_filename_sections.push_back(name_chunk);
	}

	//with our padded filename, build our lfn entries successively
	//remember that lfn entries are in REVERSE ORDER, 1-INDEXED!
	for (size_t k = n_lfn; k > 0; k--) {
		//make the particular LFN entry
		bool is_last_lfn = (k == n_lfn);
		auto lfn_k = mk_root_entry_lfn(padded_filename_sections[k - 1], file.get_short_name().checksum, k, is_last_lfn);

		//and push it into our file entry collection
		file_entry.push_n_back(lfn_k);
	}

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

	//##### FINISHED ######
	//drop the actual SFN entry at the end of our buffer
	//and finally return the output temporary
	file_entry.push_n_back(short_entry);
	return file_entry;
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
										App_Vector<Root_File_Entry_t, FS_Constants::MAX_NUM_FILES>& file_entries)
{
	//reset our root sector
	root_sector.clear();

	//put our volume name right at the beginning
	root_sector.push_n_back(volname_entry);

	//then iteratively copy over our file entries into our root sector
	for(auto& file : file_entries) root_sector.push_n_back(file);
}

