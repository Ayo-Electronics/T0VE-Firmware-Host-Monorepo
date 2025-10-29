/*
 * app_neural_memory_broker.cpp
 *
 *  Created on: Oct 28, 2025
 *      Author: govis
 */

#include "app_neural_memory_broker.hpp"


Neural_Memory_Broker::Neural_Memory_Broker(DRAM& _dram, MSC_Interface& _msc_if):
		dram(_dram), msc_if(_msc_if)
{}

void Neural_Memory_Broker::init() {
	//initialize the hardware
	dram.init();
	msc_if.init();

	//and indicate that we're willing to accept connections
	//along with attaching the memory for editing
	msc_if.connect_request();
	attach_memory();
}

//and functions to expose/hide neural memory files
void Neural_Memory_Broker::attach_memory() {
	msc_if.attach_file(block_mem_file);
	msc_if.attach_file(inputs_file);
	msc_if.attach_file(input_map_file);
	msc_if.attach_file(outputs_file);
	msc_if.attach_file(output_map_file);
}

void Neural_Memory_Broker::detach_memory() {
	msc_if.detach_file(block_mem_file);
	msc_if.detach_file(inputs_file);
	msc_if.detach_file(input_map_file);
	msc_if.detach_file(outputs_file);
	msc_if.detach_file(output_map_file);
}
