/*
 * app_mem_manager.hpp
 *
 *  Created on: Nov 3, 2025
 *      Author: govis
 */

#pragma once

#include "app_neural_memory.hpp"
#include "app_threading.hpp"
#include "app_scheduler.hpp"
#include "app_hal_dram.hpp"
#include "app_msc_if.hpp"

class Neural_Mem_Manager {
public:
	//constructor takes references to DRAM and MSC interface
	Neural_Mem_Manager(DRAM& _dram, MSC_Interface& _msc_if);

	//init function initializes dram and msc interface
	void init();

	//state variable exposure
	SUBSCRIBE_FUNC(status_nmemmanager_detected_input_size);
	SUBSCRIBE_FUNC(status_nmemmanager_detected_output_size);
	LINK_FUNC_RC(command_nmemmanager_check_io_size);
	LINK_FUNC_RC(command_nmemmanager_load_test_pattern);
	LINK_FUNC(command_nmemmanager_attach_memory);
	SUBSCRIBE_FUNC(status_nmemmanager_mem_attached);

private:
	//reference DRAM and a mass storage interface
	DRAM& dram;
	MSC_Interface& msc_if;

	//own an instance of neural memory
	Neural_Memory neural_mem;

	//functions to load some test sequences into neural memory
	void load_mem_pattern_1();	//square wave between 0 and full, discard outputs
	void load_mem_pattern_2();	//sawtooth ramp pattern, discard outputs
	void load_mem_pattern_3();	//pseudo-random pattern with same-index write-back
	void load_mem_pattern_4();	//DC value with future channel write-back

	//functions to check input/output sizes
	void check_io_mappings();

	//and functions to expose/hide neural memory files
	void attach_memory();
	void detach_memory();

	//and some state variables
	PERSISTENT((Pub_Var<uint32_t>), status_nmemmanager_detected_input_size);	//reports how many valid network inputs we've detected
	PERSISTENT((Pub_Var<uint32_t>), status_nmemmanager_detected_output_size);	//report how many valid network outputs we've detected
	Sub_Var_RC<bool> command_nmemmanager_check_io_size;							//ask the memory manager to try clocking the input/output size
	Sub_Var_RC<uint32_t> command_nmemmanager_load_test_pattern;					//set this to a non-zero value to load a test pattern into DRAM
	Sub_Var<bool> command_nmemmanager_attach_memory;							//set this to true to expose memory over MSC interface
	PERSISTENT((Pub_Var<bool>), status_nmemmanager_mem_attached);				//reports whether the memory is being exposed over the MSC interface

	//and own some mass-storage class files that we'll expose/hide when told
	MSC_File block_mem_file = 	{neural_mem.block_mem_as_bytes(), "NEURAL_BLOCK_PARAMTERS.bin"};
	MSC_File inputs_file = 		{neural_mem.inputs_as_bytes(), "NEURAL_INPUTS.bin"};
	MSC_File input_map_file = 	{neural_mem.input_map_as_bytes(), "NEURAL_INPUT_MAP.bin"};
	MSC_File outputs_file = 	{neural_mem.outputs_as_bytes(), "NEURAL_OUTPUTS.bin"};
	MSC_File output_map_file = 	{neural_mem.output_map_as_bytes(), "NEURAL_OUTPUT_MAP.bin"};

	//and a function that runs the state management
	void check_state_update();
	Scheduler check_state_update_task;
};
