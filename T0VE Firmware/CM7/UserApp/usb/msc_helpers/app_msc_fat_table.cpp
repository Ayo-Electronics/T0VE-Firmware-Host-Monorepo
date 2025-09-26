/*
 * app_msc_fat_table.cpp
 *
 *  Created on: Sep 23, 2025
 *      Author: govis
 */

#include "app_msc_fat_table.hpp"

FAT16_Table::File_Indices_t FAT16_Table::mk(std::array<MSC_File, FAT16_Table::NUMF>& files) {
	//some helper index variables
	//first valid cluster we can place a file is at cluster 2;
	//use uint16_t due to FAT16
	uint16_t file_placement_pointer = 2;

	//now go through all files
	//like using the for loop with index counter because explicit indices
	for(size_t i = 0; i < NUMF; i++) {
		//grab the file
		MSC_File& file = files[i];

		//if the file is valid (i.e. is pointing to real data, rather than being trivially constructed)
		if(file.is_valid()) {
			//compute the file size in clusters, round up
			static const size_t BYTES_PER_CLUSTER = FS_Constants::BYTES_PER_SECTOR * FS_Constants::SECTORS_PER_CLUSTER;
			size_t file_clusters = (file.get_file_size() + BYTES_PER_CLUSTER - 1) / BYTES_PER_CLUSTER;

			//place the file at our pointer, grow the pointer by the size of the file
			//and indicate that the file ends right before our updated pointer position
			file_start_indices[i] = file_placement_pointer;
			file_placement_pointer += file_clusters;
			file_end_indices[i] = file_placement_pointer - 1;
		}

		//if the file wasn't valid, put a bogus, out of range value in the corresponding index
		else {
			file_start_indices[i] = UINT16_MAX;
			file_end_indices[i] = UINT16_MAX;
		}
	}

	//we'll mark all clusters after those where our file lives as "invalid"
	//this allows us to meet the cluster count required for FAT16, but not actually allocate those clusters
	end_of_disk_index = file_placement_pointer;

	//and return the file start + end cluster indices
	//useful for when we build our root directory and data clusters
	File_Indices_t indices;
	indices.start_indices = file_start_indices;
	indices.end_indices = file_end_indices;
	return indices;
}

bool FAT16_Table::read(size_t sector_offset, std::span<uint8_t, std::dynamic_extent> sec) {
	//only allow reading the FAT table one sector at a time (or smaller intervals)
	if(sec.size() > FS_Constants::BYTES_PER_SECTOR) return false;

	//create a temporary FAT16 table; each entry will be 2 bytes
	std::array<uint16_t, FS_Constants::BYTES_PER_SECTOR/2> table_slice;
	table_slice.fill(0xFFF7); //default the cluster being bad

	//check if the address we're requesting is in a part of the table we need to build
	size_t cluster_offset = sector_offset * FS_Constants::BYTES_PER_SECTOR / 2;
	if(cluster_offset < end_of_disk_index) {
		//have to manually go through every entry of the table
		for(size_t i = 0; i < table_slice.size(); i++) {
			//compute the actual cluster index based on the cluster offset
			size_t cluster_index = i + cluster_offset;

			//check our special entries
			if		(cluster_index == 0) 					table_slice[i] = 0xFFF8;	//FAT[0] has special value
			else if	(cluster_index == 1) 					table_slice[i] = 0xFFFF;	//FAT[1] has special value
			else if (cluster_index >= end_of_disk_index) 	table_slice[i] = 0xFFF7;	//stuff beyond our disk is "corrupted"
			else if (std::count(file_end_indices.begin(), file_end_indices.end(), (uint16_t)cluster_index))
															table_slice[i] = 0xFFFF;	//file ends at that index
			else table_slice[i] = cluster_index + 1;									//otherwise, by default, file continues to the next cluster
		}
	}

	//copy our table into the input buffer and return
	memcpy(sec.data(), table_slice.data(), sec.size());
	return true;
}
