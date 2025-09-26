/*
 * app_msc_data_sector.hpp
 *
 *  Created on: Sep 25, 2025
 *      Author: govis
 */

#pragma once

#include "app_msc_constants.hpp"
#include "app_proctypes.hpp"
#include "app_utils.hpp"
#include "app_regmap_helpers.hpp"
#include "app_msc_file.hpp"
#include "app_msc_fat_table.hpp"

class Data_Sector {
	//treating this like an alias
	static const size_t NUMF = FS_Constants::MAX_NUM_FILES;

public:
	//constructor takes a reference to our file array
	//will use this array when we regenerate our sectors
	Data_Sector(std::array<MSC_File, NUMF>& _files): files(_files) {}

	//pass in the start and end indices of each of the files
	//useful for dispatching calls
	//just save what we've been passed in
	void mk(const FAT16_Table::File_Indices_t& _indices) {indices = _indices; }

	//read/write functions
	//dispatch to the files, call the read/write function of the files
	bool read(size_t sector_offset, std::span<uint8_t, std::dynamic_extent> sec);
	bool write(size_t sector_offset, std::span<uint8_t, std::dynamic_extent> sec);

private:
	//store a reference to an array of files
	std::array<MSC_File, NUMF>& files;

	//a locally maintained copy of the file indices
	//updated with calls to `mk`
	FAT16_Table::File_Indices_t indices = {0};
};
