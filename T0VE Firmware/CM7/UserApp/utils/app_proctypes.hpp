/*
 * app_types.hpp
 *
 *	Looks kinda stupid, but basically provide a central place where common datatypes are defined
 *	And also global data types related to the processor
 *
 *  Created on: Jun 9, 2025
 *      Author: govis
 */

#pragma once

extern "C" {
	#include "stm32h7xx_hal.h" //uintxx_t types
	#include "stm32h747xx.h" //register definitions
}

#include <stdbool.h>

//flag that shares byte ordering on the particular compiled processor
//useful for when we're reliant on endianness for buffer creation/byte ordering
constexpr bool PROCESSOR_IS_BIG_ENDIAN = false;

//480MHz CPU clock frequency, used for timing calculations
constexpr float CPU_FREQ_HZ = 480e6;

