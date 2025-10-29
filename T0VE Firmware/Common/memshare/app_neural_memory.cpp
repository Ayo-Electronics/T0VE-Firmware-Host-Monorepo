/*
 * app_mem_helper.cpp
 *
 *  Created on: Oct 28, 2025
 *      Author: govis
 */

#include "app_neural_memory.hpp"

//macro to make `_as_bytes` functions easier
#define AS_BYTES(span_obj) std::span<uint8_t, std::dynamic_extent>(reinterpret_cast<uint8_t*>((span_obj).data()), (span_obj).size_bytes())

//empty constructor
Neural_Memory::Neural_Memory()
{
	//initialize the input and output mapping to zeros (invalid)
	for(auto& in : NETWORK_INPUTS) 	in = 0;
	for(auto& out: NETWORK_OUTPUTS) out = 0;
}

//return a view into the external memory, where the blocks are at
std::span<Neural_Memory::Hispeed_Block_t, std::dynamic_extent> Neural_Memory::block_mem() {
	return BLOCK_MEMORY;
}

//use these functions to retrieve the chunk of memory associated with the particular variable
//useful for constructing files that can be exposed over USB
std::span<uint8_t, std::dynamic_extent> Neural_Memory::block_mem_as_bytes() 	{	return AS_BYTES(BLOCK_MEMORY);				}
std::span<uint8_t, std::dynamic_extent> Neural_Memory::inputs_as_bytes()		{	return AS_BYTES(NETWORK_INPUTS);			}
std::span<uint8_t, std::dynamic_extent> Neural_Memory::input_map_as_bytes()		{	return AS_BYTES(NETWORK_INPUT_MAPPING);		}
std::span<uint8_t, std::dynamic_extent> Neural_Memory::outputs_as_bytes()		{	return AS_BYTES(NETWORK_OUTPUTS);			}
std::span<uint8_t, std::dynamic_extent> Neural_Memory::output_map_as_bytes()	{	return AS_BYTES(NETWORK_OUTPUT_MAPPING);	}

//for the inputs/outputs, move based on the mapping information
void Neural_Memory::transfer_inputs() {
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
#if CORE_HAS_CACHE
	SCB_CleanDCache_by_Addr(
		reinterpret_cast<uint32_t*>(BLOCK_MEMORY_START),
		BLOCK_MEMORY.size_bytes()
	);
#endif

}

void Neural_Memory::transfer_outputs() {
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
#if CORE_HAS_CACHE
	SCB_CleanDCache_by_Addr(
		reinterpret_cast<uint32_t*>(NETWORK_OUTPUTS_START),
		NETWORK_OUTPUTS.size_bytes()
	);
#endif
}
