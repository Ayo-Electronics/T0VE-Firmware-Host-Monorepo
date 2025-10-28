/*
 * app_mem_helper.cpp
 *
 *  Created on: Oct 28, 2025
 *      Author: govis
 */

#include "app_mem_helper.hpp"

//constructor just saves the pointers, don't care if they're `null`
Mem_Helper::Mem_Helper(DRAM* _dram, MSC_Interface* _msc_if):
	dram(_dram),
	msc_if(_msc_if)
{}

//init the dram and mass storage interface if we provided them
void Mem_Helper::init() {
	if(dram) dram->init();
	if(msc_if) {
		msc_if->init();
		msc_if->connect_request();
	}

	//initialize the input and output mapping to zeros (invalid)
	for(auto& in : NETWORK_INPUTS) 	in = 0;
	for(auto& out: NETWORK_OUTPUTS) out = 0;
}

//return a view into the external memory, where the blocks are at
std::span<Mem_Helper::Hispeed_Block_t, std::dynamic_extent> Mem_Helper::block_mem() {
	return BLOCK_MEMORY;
}

//for attaching and detaching files, just attach/detach through the MSC interface (if present)
//if not present, just NOP
void Mem_Helper::attach_files() {
	if(msc_if) {
		msc_if->attach_file(NETWORK_INPUTS_FILE);
		msc_if->attach_file(NETWORK_INPUT_MAPPING_FILE);
		msc_if->attach_file(NETWORK_OUTPUTS_FILE);
		msc_if->attach_file(NETWORK_OUTPUT_MAPPING_FILE);
		msc_if->attach_file(BLOCK_MEM_FILE);
	}
}

void Mem_Helper::detach_files() {
	if(msc_if) {
		msc_if->detach_file(NETWORK_INPUTS_FILE);
		msc_if->detach_file(NETWORK_INPUT_MAPPING_FILE);
		msc_if->detach_file(NETWORK_OUTPUTS_FILE);
		msc_if->detach_file(NETWORK_OUTPUT_MAPPING_FILE);
		msc_if->detach_file(BLOCK_MEM_FILE);
	}
}

//for the inputs/outputs, move based on the mapping information
void Mem_Helper::transfer_inputs() {
	for(size_t i = 0; i < NETWORK_INPUTS.size(); i++) {
		//reference the input and the mapping for the particular index
		uint16_t& input = NETWORK_INPUTS[i];
		ADC_Destination_t& dest = NETWORK_INPUT_MAPPING[i];

		//if the destination is invalid break/return from the function
		if(!dest.valid()) return;

		//some sanity checking, if the destination is a throwaway, continue (should never be the case)
		//and if the particular index is out of bounds, continue
		if(dest.throwaway()) continue;
		if(dest.block_index() >= BLOCK_MEMORY.size()) continue;

		//everything is valid, push the input into the network memory
		BLOCK_MEMORY[dest.block_index()].param_vals[dest.sub_index()] = input;
	}

	//flush the cache to ensure writes go to shared memory
#ifdef CORE_HAS_CACHE
	SCB_CleanDCache_by_Addr(
		reinterpret_cast<uint32_t*>(BLOCK_MEMORY_START),
		BLOCK_MEMORY.size_bytes()
	);
#endif

}

void Mem_Helper::transfer_outputs() {
	for(size_t i = 0; i < NETWORK_INPUTS.size(); i++) {
		//reference the output and the mapping for the particular index
		uint16_t& output = NETWORK_OUTPUTS[i];
		ADC_Destination_t& source = NETWORK_OUTPUT_MAPPING[i];

		//if the destination is invalid break/return from the function
		if(!source.valid()) return;

		//some sanity checking, if the destination is a throwaway, continue (should never be the case)
		//and if the particular index is out of bounds, continue
		if(source.throwaway()) continue;
		if(source.block_index() >= BLOCK_MEMORY.size()) continue;

		//everything is valid, push the input into the network memory
		output = BLOCK_MEMORY[source.block_index()].param_vals[source.sub_index()];
	}

	//flush the cache to ensure writes go to shared memory
#ifdef CORE_HAS_CACHE
	SCB_CleanDCache_by_Addr(
		reinterpret_cast<uint32_t*>(NETWORK_OUTPUTS_START),
		NETWORK_OUTPUTS.size_bytes()
	);
#endif
}
