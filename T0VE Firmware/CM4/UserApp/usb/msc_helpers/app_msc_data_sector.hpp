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
    //trivial constructor
    Data_Sector() {}

	//pass in the files and their start and end sector indices
	//useful for dispatching calls
	//just save what we've been passed in
	void mk(std::span<MSC_File, std::dynamic_extent> _files,
			const FAT16_Table::File_Indices_t& _indices) {
		files = _files;
		indices = _indices;
	}

	//read/write functions
	//dispatch to the files, call the read/write function of the files
	bool read(size_t sector_offset, std::span<uint8_t, std::dynamic_extent> sec);
	bool write(size_t sector_offset, std::span<uint8_t, std::dynamic_extent> sec);

private:
    //store a span view of files
    std::span<MSC_File> files = {};

	//a locally maintained copy of the file indices
	//updated with calls to `mk`
	FAT16_Table::File_Indices_t indices = {};
};
