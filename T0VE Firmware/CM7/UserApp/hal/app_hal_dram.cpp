/*
 * app_hal_dram.cpp
 *
 *  Created on: Aug 8, 2025
 *      Author: govis
 */


#include "app_hal_dram.hpp"
#include "app_hal_tick.hpp" //for setup delay
#include "app_debug_vcp.hpp" //for debug output for testing
#include "app_utils.hpp" //for CPU frequency

//================================================= STATIC MEMBER DEFINITIONS ====================================================

// Define the main structure in normal RAM (no DMAMEM needed)
DRAM::DRAM_Hardware_Channel DRAM::DRAM_INTERFACE = {
	.sdram_handle = &hsdram1,
	.dram_init_function = MX_FMC_Init,
	.dram_deinit_function = {[](){ HAL_SDRAM_DeInit(&hsdram1); }}, //run the HAL de-init function for the SDRAM
	.DRAM_SIZE_BYTES = 0x800000, //8MByte
	.DRAM_BASE_ADDRESS = reinterpret_cast<void*>(0xC0000000), //base address of the DRAM in the memory map

	.DRAM_BANK = FMC_SDRAM_CMD_TARGET_BANK1,
	.AUTO_REFRESH_COUNT = 8, //2 min, can be safe with 8; only runs once
	.CAS_LATENCY = SDRAM_MODEREG_CAS_LATENCY_2,
	.BURST_LENGTH = SDRAM_MODEREG_BURST_LENGTH_1, //TODO: ALL examples seem to use burst length 1 despite enabling burst mode in the FMC controller?

	.DRAM_CLK = 100e6, //100MHz clock to the FMC after prescaler
	.DRAM_NUM_ROWS = 4096.0, //4096 rows in the DRAM
	.DRAM_FULL_REFRESH_TIME_S = 64e-3, //spec says at least 64ms, but we can be safe with 48ms
};

FASTRAM std::array<uint8_t, 65536> DRAM::test_buffer = {0};

//================================================= CONSTRUCTOR ====================================================

//constructor will just save the hardware channel to its local instance
DRAM::DRAM(DRAM::DRAM_Hardware_Channel& _hardware):
	hardware(_hardware)
{}

void DRAM::init() {
	//starts by calling the initialization function
	hardware.dram_init_function();

	//we also have to do some extra initialization to make this work
	FMC_SDRAM_CommandTypeDef command;

	//start by enabling the clock to the DRAM chip
	command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
	command.CommandTarget = hardware.DRAM_BANK;
	command.AutoRefreshNumber = 1;
	command.ModeRegisterDefinition = 0;
	HAL_SDRAM_SendCommand(hardware.sdram_handle, &command, HAL_MAX_DELAY);
	Tick::delay_ms(100); //wait at least 100us for clock to stabilize

	//precharge all memory banks
	command.CommandMode = FMC_SDRAM_CMD_PALL;
	command.CommandTarget = hardware.DRAM_BANK;
	command.AutoRefreshNumber = 1;
	command.ModeRegisterDefinition = 0;
	HAL_SDRAM_SendCommand(hardware.sdram_handle, &command, HAL_MAX_DELAY);

	//DRAM internal circuitry stabilization via auto-refresh
	command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
	command.CommandTarget = hardware.DRAM_BANK;
	command.AutoRefreshNumber = hardware.AUTO_REFRESH_COUNT;
	command.ModeRegisterDefinition = 0;
	HAL_SDRAM_SendCommand(hardware.sdram_handle, &command, HAL_MAX_DELAY);

	//set up the mode register
	//some of these are forced to values that are most compatible
	uint32_t mode =
	    hardware.BURST_LENGTH |
	    SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
	    hardware.CAS_LATENCY |
	    SDRAM_MODEREG_OPERATING_MODE_STANDARD |
	    SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;
	command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
	command.CommandTarget = hardware.DRAM_BANK;
	command.ModeRegisterDefinition = mode;
	command.AutoRefreshNumber = 1;
	HAL_SDRAM_SendCommand(hardware.sdram_handle, &command, HAL_MAX_DELAY);

	//set the DRAM refresh rate
	float DRAM_CYCLES_BETWEEN_REFRESH = hardware.DRAM_FULL_REFRESH_TIME_S / hardware.DRAM_NUM_ROWS * hardware.DRAM_CLK;
	uint32_t refresh_rate = ((uint32_t)DRAM_CYCLES_BETWEEN_REFRESH) - 20; // 20 comes from ST's correction factor, page 914 in RM0399
	HAL_SDRAM_ProgramRefreshRate(hardware.sdram_handle, refresh_rate);
}

void DRAM::de_init() {
	//just run the de-initialization function of the peripheral;
	//not likely to be used
	hardware.dram_deinit_function();
}

void DRAM::test() {
	// First run a simple write/read test to verify basic functionality
	VCP_Debug::print("=== DRAM Basic Functionality Test ===\n");
	
	// Test simple write and read
	volatile uint32_t* test_addr = static_cast<volatile uint32_t*>(hardware.DRAM_BASE_ADDRESS);
	uint32_t test_pattern = 0x12345678;
	
	VCP_Debug::print("Writing test pattern: 0x" + std::to_string(test_pattern) + "\n");
	test_addr[0] = test_pattern;
	
	uint32_t read_value = test_addr[0];
	VCP_Debug::print("Read value: 0x" + std::to_string(read_value) + "\n");
	
	if (read_value == test_pattern) {
		VCP_Debug::print("SUCCESS: Write/Read test passed!\n");
	} else {
		VCP_Debug::print("FAILED: Write/Read test failed!\n");
		VCP_Debug::print("Expected: 0x" + std::to_string(test_pattern) + ", Got: 0x" + std::to_string(read_value) + "\n");
		return; // Don't run performance tests if basic functionality fails
	}
	
	// Test multiple locations
	VCP_Debug::print("\nTesting multiple locations...\n");
	uint32_t fail_count = 0;
	for (uint32_t i = 0; i < hardware.DRAM_SIZE_BYTES/sizeof(test_addr[0]); i++) test_addr[i] = i; //start by writing to all addresses
	for (uint32_t i = 0; i < hardware.DRAM_SIZE_BYTES/sizeof(test_addr[0]); i++) {
		uint32_t read = test_addr[i];
		if (read != i) fail_count++;
	}
	
	if (fail_count == 0) {
		VCP_Debug::print("SUCCESS: Multiple location test passed!\n");
	} else {
		VCP_Debug::print("FAILED: Multiple location test failed! (" + std::to_string(fail_count) + " failures)\n");
		return;
	}
	
	VCP_Debug::print("\n=== DRAM Performance Test Results ===\n");
	
	// Initialize the debug timer for cycle counting
	dwt_init();
	
	// Run all performance tests
	float seq_write_mbps = test_seq_write();
	float seq_read_mbps = test_seq_read();
	float random_write_mbps = test_random_write();
	float random_read_mbps = test_random_read();
	
	// Print test results
	VCP_Debug::print("CPU Clock: " + f2s<1>(CPU_FREQ_HZ / 1e6) + " MHz\n");
	VCP_Debug::print("DRAM Size: " + f2s<1>(hardware.DRAM_SIZE_BYTES / 1024.0 / 1024.0) + " MB\n");
	VCP_Debug::print("\n");
	
	VCP_Debug::print("Sequential Write: " + f2s<3>(seq_write_mbps) + " MBps \n");
	VCP_Debug::print("Sequential Read:  " + f2s<3>(seq_read_mbps) + " MBps \n");
	VCP_Debug::print("Random Write:     " + f2s<3>(random_write_mbps) + " MBps \n");
	VCP_Debug::print("Random Read:      " + f2s<3>(random_read_mbps) + " MBps \n");
	VCP_Debug::print("\n");
	
	// Calculate and display efficiency metrics
	float write_efficiency = (seq_write_mbps / seq_read_mbps) * 100.0f;
	float random_efficiency = (random_read_mbps / seq_read_mbps) * 100.0f;
	
	VCP_Debug::print("Write/Read Ratio: " + std::to_string((int)write_efficiency) + "%\n");
	VCP_Debug::print("Random/Sequential Ratio: " + std::to_string((int)random_efficiency) + "%\n");
	VCP_Debug::print("=====================================\n");
}


//================================================== PRIVATE MEMBER FUNCTIONS ===================================================
void DRAM::dwt_init() {
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;	//enable the DWT unit
	DWT->CYCCNT = 0;	//reset the cycle counter
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; //enable the cycle counter
}

uint32_t DRAM::dwt_get_cycles() {
	return DWT->CYCCNT; //return the current cycle count
}

float DRAM::test_seq_write() {
	//write a bunch of bytes to DRAM
	uint32_t start_cycles = dwt_get_cycles();
	memcpy(hardware.DRAM_BASE_ADDRESS, test_buffer.begin(), test_buffer.size()); // memcpy from our test buffer
	SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(hardware.DRAM_BASE_ADDRESS), test_buffer.size()); //actually write the data to the DRAM if the data is cached
	uint32_t end_cycles = dwt_get_cycles();
	
	//compute bandwidth in MB/s
	return (test_buffer.size() / (1024.0 * 1024.0)) * (CPU_FREQ_HZ/(end_cycles - start_cycles)); //return in units of MB/s
}

float DRAM::test_seq_read() {
	//invalidate the cache for the data we're about to read
	SCB_InvalidateDCache_by_Addr(hardware.DRAM_BASE_ADDRESS, test_buffer.size());

	uint32_t start_cycles = dwt_get_cycles();
	memcpy(test_buffer.begin(), hardware.DRAM_BASE_ADDRESS, test_buffer.size()); // Use memcpy for better performance
	uint32_t end_cycles = dwt_get_cycles();
	
	//compute bandwidth in MB/s
	return (test_buffer.size() / (1024.0 * 1024.0)) * (CPU_FREQ_HZ/(end_cycles - start_cycles)); //return in units of MB/s
}

float DRAM::test_random_write() {
	// 32-bit stride-based write with caches enabled; assumes DRAM size is power-of-two
	volatile uint32_t* const dram_words = static_cast<volatile uint32_t*>(hardware.DRAM_BASE_ADDRESS);
	const uint32_t num_words = hardware.DRAM_SIZE_BYTES / sizeof(uint32_t);
	const uint32_t mask = num_words - 1U;
	const uint32_t stride_words = 8191U; // odd stride

	uint32_t idx = 0U;
	uint32_t start_cycles = dwt_get_cycles();
	for (uint32_t i = 0; i < num_words; i++) {
		idx = (idx + stride_words) & mask;
		dram_words[idx] = 0xDEDEDEDEU;
	}
	//flush the cache and grab the number of cycles
	SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(hardware.DRAM_BASE_ADDRESS), hardware.DRAM_SIZE_BYTES);
	uint32_t end_cycles = dwt_get_cycles();
	
	//compute bandwidth in MB/s
	return (hardware.DRAM_SIZE_BYTES / (1024.0 * 1024.0)) * (CPU_FREQ_HZ/(end_cycles - start_cycles)); //return in units of MB/s
}

float DRAM::test_random_read() {
	// 32-bit stride-based read with caches enabled; assumes DRAM size is power-of-two
	volatile uint32_t* const dram_words = static_cast<volatile uint32_t*>(hardware.DRAM_BASE_ADDRESS);
	const uint32_t num_words = hardware.DRAM_SIZE_BYTES / sizeof(uint32_t);
	const uint32_t mask = num_words - 1U;
	const uint32_t stride_words = 8191U;

	// Invalidate before measuring to force fetches from external memory
	SCB_InvalidateDCache_by_Addr(hardware.DRAM_BASE_ADDRESS, hardware.DRAM_SIZE_BYTES);

	uint32_t idx = 0U;
	volatile uint32_t sink = 0U;
	uint32_t start_cycles = dwt_get_cycles();
	for (uint32_t i = 0; i < num_words; i++) {
		sink = dram_words[idx];
		idx = (idx + stride_words) & mask;
	}
	uint32_t end_cycles = dwt_get_cycles();
	(void)sink;

	//compute bandwidth in MB/s
	return (hardware.DRAM_SIZE_BYTES / (1024.0 * 1024.0)) * (CPU_FREQ_HZ/(end_cycles - start_cycles)); //return in units of MB/s
}
