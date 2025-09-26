/*
 * app_msc_boot_sectors.cpp
 *
 *  Created on: Sep 23, 2025
 *      Author: govis
 */


#include "app_msc_boot_sector.hpp"

void Boot_Sector::mk(std::array<uint8_t, 11> volume_label, const uint32_t UID) {
	//some magic constants for FAT16 supposedly
	//JMP, NOP, OEM NAME
	boot_sector[0] = 0xEB;
	boot_sector[1] = 0x3C;
	boot_sector[2] = 0x90;
	static const std::array<uint8_t, 8> OEM = s2a("MSDOS5.0");
	std::copy(OEM.begin(), OEM.end(), boot_sector.begin() + 3);

	//bios parameter block fields
	//some initialized to magic numbers
	Regmap_Field(11, 0, 16, false, boot_sector) = FS_Constants::BYTES_PER_SECTOR;	//bytes per sector
	Regmap_Field(13, 0, 8,  false, boot_sector) = FS_Constants::SECTORS_PER_CLUSTER;//sectors per cluster
	Regmap_Field(14, 0, 16, false, boot_sector) = 1;								//reserved sectors before FAT1, boot sector
	Regmap_Field(16, 0, 8,  false, boot_sector) = 2;								//num redundant FAT tables, 2 for FAT16
	Regmap_Field(17, 0, 16, false, boot_sector) = FS_Constants::MAX_ROOT_ENTRIES;	//max # of root entries (fixed in FAT16)
	Regmap_Field(19, 0, 16, false, boot_sector) = 0;								//total num sectors (if < 65536)
	Regmap_Field(21, 0, 8,  false, boot_sector) = 0xF8;								//media descriptor (legacy), 0xF8 for hard disk
	Regmap_Field(22, 0, 16, false, boot_sector) = FS_Constants::FAT_TABLE_SECTORS;	//sectors needed for FAT table
	Regmap_Field(24, 0, 16, false, boot_sector) = 63;								//sectors per track (legacy), 63 is common default
	Regmap_Field(26, 0, 16, false, boot_sector) = 255;								//number of heads (legacy), 255 is common default
	Regmap_Field(28, 0, 32, false, boot_sector) = 0;								//number of sectors before start of partition (0 if whole disk)
	Regmap_Field(32, 0, 32, false, boot_sector) = FS_Constants::TOTAL_NUM_SECTORS;	//total num sectors (if >= 65536)

	//Extended BPB
	Regmap_Field(36, 0, 8,  false, boot_sector) = 0x80; 				//drive number (legacy)
	Regmap_Field(37, 0, 8,  false, boot_sector) = 0x00;					//reserved value, explicitly 0
	Regmap_Field(38, 0, 8,  false, boot_sector) = 0x29;					//boot signature, set to 0x29
	Regmap_Field(39, 0, 32, false, boot_sector) = UID;					//Volume ID, not used by OS really, setting to STM32 UID

	//Volume label (11 chars, space padded)
	std::copy(volume_label.begin(), volume_label.end(), boot_sector.begin() + 43);

	//File system type (8 chars, space padded)
	//have to use FAT16 if we have more than 4084 clusters
	static const std::array<uint8_t, 8> FSTYPE_FAT16 = s2a("FAT16   ");
	std::copy(FSTYPE_FAT16.begin(), FSTYPE_FAT16.end(), boot_sector.begin() + 54);

	//Boot signature
	boot_sector[510] = 0x55;
	boot_sector[511] = 0xAA;
}

bool Boot_Sector::read(size_t sector_offset, std::span<uint8_t, std::dynamic_extent> sec) {
	//just a single sector, error out if we're requesting sector offset > 0
	//also error if we wanna copy more than than a single sector
	if(sector_offset > 0) return false;
	if(sec.size() > FS_Constants::BYTES_PER_SECTOR) return false;

	//copy what we generated before
	//but only copy up to the size of `sec`
	std::copy(boot_sector.begin(), boot_sector.begin() + sec.size(), sec.begin());
	return true;
}
