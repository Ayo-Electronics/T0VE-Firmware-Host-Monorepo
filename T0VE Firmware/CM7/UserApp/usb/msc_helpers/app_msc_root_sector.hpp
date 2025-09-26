/*
 * app_msc_root_sector.hpp
 *
 *  Created on: Sep 23, 2025
 *      Author: govis
 */

#pragma once

#include <app_msc_constants.hpp>
#include "app_proctypes.hpp"
#include "app_utils.hpp"
#include "app_regmap_helpers.hpp"
#include "app_msc_file.hpp"
#include "app_msc_fat_table.hpp"

class Root_Sector {

	//shared constants and typedefs
	static constexpr size_t LFN_CHARS_PER_ENTRY = 13;	//how many characters an LFN entry can carry
	static constexpr size_t REQUIRED_LFN_ENTRIES = (FS_Constants::FILENAME_MAX_LENGTH + LFN_CHARS_PER_ENTRY - 1) / LFN_CHARS_PER_ENTRY;
	using Root_Data_Entry_t = 	std::array<uint8_t, 32>;
	using Root_LFN_Entry_t = 	std::array<uint8_t, 32>;
	using Root_File_Buffer_t = 	std::array<uint8_t, sizeof(Root_Data_Entry_t) * (REQUIRED_LFN_ENTRIES + 1)>;	//how much space we should allocate when building a file entry
	using Root_File_Entry_t = 	std::span<uint8_t, std::dynamic_extent>;	//since #of LFN entries vary based on file names, have to handle
	using Root_Sector_t = 		std::array<uint8_t, sizeof(Root_Data_Entry_t) + FS_Constants::MAX_NUM_FILES*sizeof(Root_File_Buffer_t)>;	//worst case, may be smaller

public:
	//trivial constructor
	Root_Sector() {}

	//function that generates the root sector
	//pass in a volume name, an array of files, and the emulated start clusters of those files
	void mk(std::array<uint8_t, 11> volume_label,
			std::array<MSC_File, FS_Constants::MAX_NUM_FILES>& files,
			FAT16_Table::File_Indices_t& cluster_indices);

	//accessor function
	bool read(size_t sector_offset, std::span<uint8_t, std::dynamic_extent> sec);

private:
	//helper functions to make the root entries
	Root_File_Entry_t 	mk_root_entry_file(MSC_File& file, uint16_t cluster, Root_File_Buffer_t& file_buffer);
	Root_LFN_Entry_t 	mk_root_entry_lfn(	std::span<uint8_t, LFN_CHARS_PER_ENTRY> lfn_chars,
											uint8_t sfname_checksum,
											size_t lfn_index, bool last = false);
	Root_Data_Entry_t	mk_root_entry_volname(std::array<uint8_t, 11> vol_name);
	void update_root_sector(Root_Data_Entry_t& volname_entry, std::array<Root_File_Entry_t, FS_Constants::MAX_NUM_FILES>& file_entries);


	//and a container for the "maintained" parts of the Root Sectors
	//any access beyond these root sectors will received zeros
	Root_Sector_t root_sector;
};
