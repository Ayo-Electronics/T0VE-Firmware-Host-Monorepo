/*
 * app_msc_boot_sector.hpp
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

#include "app_string.hpp"


class Boot_Sector {
using Boot_Sector_t = std::array<uint8_t, FS_Constants::BYTES_PER_SECTOR>;
public:
	//trivial constructor
	Boot_Sector() {}

	//function that generates the sector
	//pass in a volume label and UID; BOOT sector gets generated based above constants otherwise
	void mk(App_String<11, ' '>& volume_label, const uint32_t UID = 0x12345678);

	//accessor functions
	//copies boot sector into `sec`
	//returns true if sucess, false if not
	bool read(size_t sector_offset, std::span<uint8_t, std::dynamic_extent> sec);

private:
	//not that big; can just store the boot sector directly
	Boot_Sector_t boot_sector = {0};
};
