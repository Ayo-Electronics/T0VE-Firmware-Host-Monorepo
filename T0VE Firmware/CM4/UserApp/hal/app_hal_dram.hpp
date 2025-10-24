/*
 * app_hal_dram.hpp
 *
 *  Created on: Aug 8, 2025
 *      Author: govis
 */

#pragma once

#include "fmc.h"

#include "app_utils.hpp"

//helper macro to place data we want in external memory in the right place
#define EXTMEM 	__attribute__((section(".EXTMEM_Section"), aligned(32)))
#define FASTRAM __attribute__((section(".FAST_SRAM_Section"), aligned(32)))

class DRAM {
public:
	//================================ TYPEDEFS ================================
	//weirdly these aren't provided via STM32 HAL, so defining them (in a more C++ way) here
	static const uint16_t SDRAM_MODEREG_BURST_LENGTH_1 = 0x0;
	static const uint16_t SDRAM_MODEREG_BURST_LENGTH_2 = 0x1;
	static const uint16_t SDRAM_MODEREG_BURST_LENGTH_4 = 0x2;
	static const uint16_t SDRAM_MODEREG_BURST_LENGTH_8 = 0x3;
	static const uint16_t SDRAM_MODEREG_BURST_LENGTH_PAGE = 0x7;
	static const uint16_t SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL = 0x0;
	static const uint16_t SDRAM_MODEREG_BURST_TYPE_INTERLEAVED = 0x8;
	static const uint16_t SDRAM_MODEREG_CAS_LATENCY_2 = 0x20;
	static const uint16_t SDRAM_MODEREG_CAS_LATENCY_3 = 0x30;
	static const uint16_t SDRAM_MODEREG_OPERATING_MODE_STANDARD = 0x0;
	static const uint16_t SDRAM_MODEREG_WRITEBURST_MODE_PROGRAMMED = 0x0;
	static const uint16_t SDRAM_MODEREG_WRITEBURST_MODE_SINGLE = 0x0200;

	//NOTE: need to place these in a section that is accessible by DMA
	//use the `DMAMEM` macro to place these in the appropriate section
	struct DRAM_Hardware_Channel {
		SDRAM_HandleTypeDef* const sdram_handle;
		//these are the functions that will be called to initialize the I2C and DMA peripherals
		const Callback_Function<> dram_init_function;
		const Callback_Function<> dram_deinit_function;
		const size_t DRAM_SIZE_BYTES; //number of bytes of the DRAM
		void* const DRAM_BASE_ADDRESS;

		//extra stuff for DRAM configuration
		const uint32_t DRAM_BANK;
		const uint32_t AUTO_REFRESH_COUNT; //number of auto refreshes required to stabilize DRAM circuitry after init
		const uint32_t CAS_LATENCY; //STM32Cube configured CAS latency
		const uint32_t BURST_LENGTH; //write burst length preference

		//and for DRAM refresh timing, we need a couple more paramters
		const float DRAM_CLK; //clock frequency to the DRAM
		const float DRAM_NUM_ROWS; //number of rows DRAM has
		const float DRAM_FULL_REFRESH_TIME_S; //how frequently the entire chip needs to get updated
	};

	static DRAM_Hardware_Channel DRAM_INTERFACE;

	void init();
	void de_init();
	void test(); //modify function signature as necessary
	size_t size() { return hardware.DRAM_SIZE_BYTES; }
	void* const start() { return hardware.DRAM_BASE_ADDRESS; }

	//========================= CONSTRUCTORS, DESTRUCTORS, OVERLOADS =========================
	DRAM(DRAM_Hardware_Channel& _hardware);

	//delete assignment operator and copy constructor
	//in order to prevent hardware conflicts
	DRAM(DRAM const& other) = delete;
	void operator=(DRAM const& other) = delete;

private:
	//testing utility functions
	void dwt_init(); //debug timer
	uint32_t dwt_get_cycles(); //debug timer
	float test_seq_write(); //all memory bandwidth tests will return speed in MB/s
	float test_seq_read();
	float test_random_write();
	float test_random_read();

	//reference a hardware channel defining configuration
	DRAM_Hardware_Channel& hardware;

	//also own a large buffer (64k) for RAM testing
	//place this in AXI SRAM such that it's still pretty fast
	static FASTRAM std::array<uint8_t, 65536> test_buffer;
};
