/*
 * app_msc_data_sector.cpp
 *
 *  Created on: Sep 25, 2025
 *      Author: govis
 */

#include "app_msc_data_sector.hpp"

//dispatch a read request to a particular file
bool Data_Sector::read(size_t sector_offset, std::span<uint8_t, std::dynamic_extent> sec) {
	//output temporary
	bool success = false;

	//go through all of our files
	for(size_t i = 0; i < files.size(); i++) {
		//get the file, start and end indices (in sectors)
		MSC_File& file = files[i];
		uint16_t start_cluster = indices.start_indices[i];
		uint16_t end_cluster   = indices.end_indices[i];

		//clusters should start at 2 for FAT16 data region
		if(start_cluster < 2 || end_cluster < 2) continue; //sanity check

		//map clusters to data-sector-relative sectors (end is exclusive)
		//REMEMBER that the FAT16 system start the data sector at cluster 2! Hence we have to subtract 2 from the cluster counts
		size_t start_sector = static_cast<size_t>(start_cluster - 2) * FS_Constants::SECTORS_PER_CLUSTER;
		size_t end_sector   = static_cast<size_t>(end_cluster - 1) * FS_Constants::SECTORS_PER_CLUSTER; // inclusive cluster -> exclusive sector end, -2 +1

		//check if the sector offset is in range
		if(sector_offset < start_sector) continue;
		if(sector_offset >= end_sector) continue;

		//if we're here, this is the file we wanna service
		//compute the necessary offsets, zero fill the buffer (for short reads), and try to read the file
		size_t byte_offset = (sector_offset - start_sector) * FS_Constants::BYTES_PER_SECTOR;
		std::fill(sec.begin(), sec.end(), 0);
		success  = file.read(byte_offset, sec) > 0;
		break;
	}

	//and return our output variable
	return success;
}

//dispatch a write request to a particular file
bool Data_Sector::write(size_t sector_offset, std::span<uint8_t, std::dynamic_extent> sec) {
	//output temporary
	bool success = false;

	//go through all of our files
	for(size_t i = 0; i < files.size(); i++) {
		//get the file
		MSC_File& file = files[i];

		//get clusters and validate
		uint16_t start_cluster = indices.start_indices[i];
		uint16_t end_cluster   = indices.end_indices[i];
		if(start_cluster < 2 || end_cluster < 2) continue; //sanity check

		//map clusters to data-sector-relative sectors (end is exclusive)
		//REMEMBER that the FAT16 system start the data sector at cluster 2! Hence we have to subtract 2 from the cluster counts
		size_t start_sector = static_cast<size_t>(start_cluster - 2) * FS_Constants::SECTORS_PER_CLUSTER;
		size_t end_sector   = static_cast<size_t>(end_cluster - 1) * FS_Constants::SECTORS_PER_CLUSTER; // inclusive cluster -> exclusive sector end

		//check if the sector offset is in range
		if(sector_offset < start_sector) continue;
		if(sector_offset >= end_sector) continue;

		//if we're here, this is the file we wanna service
		//compute the necessary offsets, and try to read the file
		size_t byte_offset = (sector_offset - start_sector) * FS_Constants::BYTES_PER_SECTOR;
		success = file.write(byte_offset, sec) > 0;
		break;
	}

	//and return our output variable
	return success;
}







