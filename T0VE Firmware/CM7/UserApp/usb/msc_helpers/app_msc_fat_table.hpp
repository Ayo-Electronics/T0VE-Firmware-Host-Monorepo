/*
 * app_msc_fat_table.hpp
 *
 *  Created on: Sep 23, 2025
 *      Author: govis
 *
 *  Scratch work for me:
 *  For sequentially allocated files, the FAT table addresses will be:
 *   - Almost always pointing to [index + 1]
 *   - Will be 0x0000 if a file starts there (maintain an array of "file start addresses")
 *   - Will be 0xFFF8 if a file ends there (maintain an array of "file end addresses")
 *   - Will be 0xFFF7 if an index is out of a file range (maintain a "end of valid storage" index)
 */

#pragma once

#include "app_msc_constants.hpp"
#include "app_proctypes.hpp"
#include "app_utils.hpp"
#include "app_regmap_helpers.hpp"
#include "app_msc_file.hpp"

#include "app_vector.hpp"

class FAT16_Table {
	//treating this like an alias
	static const size_t NUMF = FS_Constants::MAX_NUM_FILES;

public:
	//============== TYPEDEFS =============

	//simple struct to wrap the start and end cluster indices
	//useful for mapping where files are in our emulated file system
	struct File_Indices_t {
		App_Vector<uint16_t, NUMF> start_indices;
		App_Vector<uint16_t, NUMF> end_indices;
	};

	//============= PUBLIC METHODS ============

	//trivial constructor
	FAT16_Table() {}

    //function that generates the table
    //accepts a span of files (may be fewer than NUMF)
    //returns the starting cluster address of each of the files
    File_Indices_t mk(std::span<MSC_File, std::dynamic_extent> files);

	//accessor functions
	//generate the particular sector of the FAT table on the fly
	bool read(size_t sector_offset, std::span<uint8_t, std::dynamic_extent> sec);

private:
	//a couple arrays of indices, see the notes at the top
	//these are what get updated during mk();
	App_Vector<uint16_t, NUMF> file_start_indices = {};
	App_Vector<uint16_t, NUMF> file_end_indices = {};
	uint16_t end_of_disk_index = 0;
};


