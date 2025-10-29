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

#ifdef __cplusplus
extern "C" {
#endif
	#include "stm32h7xx_hal.h" //uintxx_t types
	#include "stm32h747xx.h" //register definitions
#ifdef __cplusplus
}
#endif
#include <stdbool.h>

//compile-time constant to that can be used to conditionally compile cache flushes
#define CORE_HAS_CACHE (defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U))

//flag that shares byte ordering on the particular compiled processor
//useful for when we're reliant on endianness for buffer creation/byte ordering
constexpr bool PROCESSOR_IS_BIG_ENDIAN = false;

//480MHz CPU clock frequency, used for timing calculations
constexpr float CPU_FREQ_HZ = 480e6;

