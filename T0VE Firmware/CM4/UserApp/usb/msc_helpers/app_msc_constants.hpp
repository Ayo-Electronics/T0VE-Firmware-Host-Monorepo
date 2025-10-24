/*
 * app_msc_helpers.hpp
 *
 *  Created on: Sep 22, 2025
 *      Author: govis
 */

#pragma once

#include "tusb_config.h"
#include "app_proctypes.hpp"
#include "app_utils.hpp"
#include "app_regmap_helpers.hpp"

class FS_Constants {
public:
	//fixed size containers for filename
	//makes static allocation easier
	static constexpr size_t FILENAME_MAX_LENGTH = 39;

	//want static allocation, so determine once how many files we want to support
	static constexpr size_t MAX_NUM_FILES = 8;

	static constexpr size_t BYTES_PER_SECTOR = 512;		//keep this value constant
	static constexpr size_t SECTORS_PER_CLUSTER = 8; 	//4k cluster size, adjust for smaller larger storage volume
	static constexpr size_t NUM_DATA_CLUSTERS = 32768;	//keep 4085 < this < 65512 (or something like that) to maintain FAT16 classification
	static constexpr size_t MAX_ROOT_ENTRIES = 512;		//keep this 512 for FAT16

	static constexpr size_t FAT_TABLE_SECTORS = (((NUM_DATA_CLUSTERS + 2) * 2) / BYTES_PER_SECTOR) + 1;	//2 special entries in FAT table, will always have extra space
	static constexpr size_t ROOT_NUM_SECTORS = ((MAX_ROOT_ENTRIES * 32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR;
	static constexpr size_t TOTAL_NUM_SECTORS = (NUM_DATA_CLUSTERS * SECTORS_PER_CLUSTER) + (2 * FAT_TABLE_SECTORS) + ROOT_NUM_SECTORS + 1;

	//and have some constants about the sector locations of various portions of the file system
	//useful for dispatching
	//NOTE: the `END` field is EXCLUSIVE! i.e. the particular structure lies at [START, END)
	static constexpr size_t BOOT_SECTOR_START = 0;
	static constexpr size_t BOOT_SECTOR_END = BOOT_SECTOR_START + 1;
	static constexpr size_t FAT1_SECTOR_START = BOOT_SECTOR_END;
	static constexpr size_t FAT1_SECTOR_END = FAT1_SECTOR_START + FAT_TABLE_SECTORS;
	static constexpr size_t FAT2_SECTOR_START = FAT1_SECTOR_END;
	static constexpr size_t FAT2_SECTOR_END = FAT2_SECTOR_START + FAT_TABLE_SECTORS;
	static constexpr size_t ROOT_SECTOR_START = FAT2_SECTOR_END;
	static constexpr size_t ROOT_SECTOR_END = ROOT_SECTOR_START + ROOT_NUM_SECTORS;
	static constexpr size_t DATA_SECTOR_START = ROOT_SECTOR_END;
	static constexpr size_t DATA_SECTOR_END = TOTAL_NUM_SECTORS;

	//and a typedef to define a container for a data sector
	using Sector_t = std::array<uint8_t, BYTES_PER_SECTOR>;

private:
	//private constructor, so can't be instantiated
	FS_Constants() {}
};

//sanity check our tinyusb config
//have to shoehorn this into here rather than in the config file directly due to C/C++ issues
static_assert(	(CFG_TUD_MSC_EP_BUFSIZE % FS_Constants::BYTES_PER_SECTOR) == 0,
				"MSC Buffer Size + FS Sector Size Mismatch. Make sure buffer is a multiple of sector size!");

