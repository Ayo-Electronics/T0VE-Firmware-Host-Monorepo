/*
 * app_neural_memory_broker.hpp
 *
 *  Created on: Oct 28, 2025
 *      Author: govis
 *
 *  a helper class to wrap neural memory accesses with mass storage class
 */

#pragma once

#include "app_hal_dram.hpp"
#include "app_msc_if.hpp"
#include "app_msc_file.hpp"
#include "app_neural_memory.hpp"

#include "app_utils.hpp"
#include "app_proctypes.hpp"

class Neural_Memory_Broker {
public:
	//constructor takes references to DRAM and MSC interface
	Neural_Memory_Broker(DRAM& _dram, MSC_Interface& _msc_if);

	//init function initializes dram and msc interface
	void init();

	//and functions to expose/hide neural memory files
	void attach_memory();
	void detach_memory();

private:
	//reference DRAM and a mass storage interface
	DRAM& dram;
	MSC_Interface& msc_if;

	//own an instance of neural memory
	Neural_Memory neural_mem;

	//and own some mass-storage class files that we'll expose/hide when told
	MSC_File block_mem_file = 	{neural_mem.block_mem_as_bytes(), "NEURAL_BLOCK_PARAMTERS.bin"};
	MSC_File inputs_file = 		{neural_mem.inputs_as_bytes(), "NEURAL_INPUTS.bin"};
	MSC_File input_map_file = 	{neural_mem.input_map_as_bytes(), "NEURAL_INPUT_MAP.bin"};
	MSC_File outputs_file = 	{neural_mem.outputs_as_bytes(), "NEURAL_OUTPUTS.bin"};
	MSC_File output_map_file = 	{neural_mem.output_map_as_bytes(), "NEURAL_OUTPUT_MAP.bin"};
};
