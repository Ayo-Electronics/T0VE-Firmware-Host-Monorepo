/*
 * app_mem_manager.cpp
 *
 *  Created on: Nov 4, 2025
 *      Author: govis
 */

#include "app_mem_manager.hpp"


Neural_Mem_Manager::Neural_Mem_Manager(DRAM& _dram, MSC_Interface& _msc_if):
		dram(_dram), msc_if(_msc_if)
{}

void Neural_Mem_Manager::init() {
	//initialize the hardware
	dram.init();
	msc_if.init();

	//and indicate that we're willing to accept connections
	//along with attaching the memory for editing
	msc_if.connect_request();

	//and start our state update task
	check_state_update_task.schedule_interval_ms(	BIND_CALLBACK(this, check_state_update),
													Scheduler::INTERVAL_EVERY_ITERATION);
}

//============= PRIVATE FUNCTIONS ==============
void Neural_Mem_Manager::check_state_update() {
	//check if we want to attach/detach our memory
	if(command_nmemmanager_attach_memory.check()) {
		if(command_nmemmanager_attach_memory.read()) 	attach_memory();
		else 											detach_memory();
	}

	//check if we wanna analyze our discovered input/output size
	if(command_nmemmanager_check_io_size.check()) {
		check_io_mappings();
	}

	//and if we want to load some test patterns
	if(command_nmemmanager_load_test_pattern.check()) {
		//load the test pattern accordingly
		auto test_pattern = command_nmemmanager_load_test_pattern.read();
		if(test_pattern == 1) load_mem_pattern_1();
		if(test_pattern == 2) load_mem_pattern_2();
		if(test_pattern == 3) load_mem_pattern_3();
		if(test_pattern == 4) load_mem_pattern_4();

		//and acknowledge the test pattern load flag
		command_nmemmanager_load_test_pattern.acknowledge_reset();
	}
}

void Neural_Mem_Manager::check_io_mappings() {
	//don't run the test if we cleared the flag early
	if(!command_nmemmanager_check_io_size.read()) return;

	//grab the input mapping
	auto imap = neural_mem.input_mapping();

	//go through these inputs, and find the first invalid index
	//this is where the input transfer will stop; corresponds with the input size
	for(size_t i = 0; i < imap.size(); i++) {
		auto& dest = imap[i];
		if(!dest.valid()) {
			status_nmemmanager_detected_input_size.publish(i);
			break;
		}
	}

	//do the exact same thing with the output mappings
	//grab the output mapping
	auto omap = neural_mem.output_mapping();

	//go through these inputs, and find the first invalid index
	//this is where the input transfer will stop; corresponds with the input size
	for(size_t i = 0; i < omap.size(); i++) {
		auto& dest = omap[i];
		if(!dest.valid()) {
			status_nmemmanager_detected_output_size.publish(i);
			break;
		}
	}

	//and acknowledge our command flag
	command_nmemmanager_check_io_size.acknowledge_reset();
}

//and functions to expose/hide neural memory files
void Neural_Mem_Manager::attach_memory() {
	msc_if.attach_file(block_mem_file);
	msc_if.attach_file(inputs_file);
	msc_if.attach_file(input_map_file);
	msc_if.attach_file(outputs_file);
	msc_if.attach_file(output_map_file);

	//and mark that the files are attached
	status_nmemmanager_mem_attached.publish(true);
}

void Neural_Mem_Manager::detach_memory() {
	msc_if.detach_file(block_mem_file);
	msc_if.detach_file(inputs_file);
	msc_if.detach_file(input_map_file);
	msc_if.detach_file(outputs_file);
	msc_if.detach_file(output_map_file);

	//and mark that the files are detached
	status_nmemmanager_mem_attached.publish(false);
}

//=============== MEMORY TEST PATTERN LOADING ===============
//square wave between 0 and full, discard outputs
void Neural_Mem_Manager::load_mem_pattern_1() {
	detach_memory(); 						//prevent editing while we're loading our test pattern
	neural_mem.clean();						//zero out all arrays
	auto sequence = neural_mem.block_mem();	//grab the memory as an array

	//go through the memory array
	for(size_t i = 0; i < sequence.size() - 1; i++) {
		//set the parameter values of the particular block to either
		//full scale or zero
		uint16_t param_val = (i % 2 == 0) ? UINT16_MAX : 0;

		//and make a throwaway block with the particular param vals
		sequence[i] = Neural_Memory::Hispeed_Block_t::mk_throwaway({param_val, param_val, param_val, param_val});
	}

	//and the last block should be a terminator
	sequence.back() = Neural_Memory::Hispeed_Block_t::mk_term();
	attach_memory(); //allow editing after loading our test pattern
}

//sawtooth pattern, discard outputs
void Neural_Mem_Manager::load_mem_pattern_2() {
	detach_memory(); 						//prevent editing while we're loading our test pattern
	neural_mem.clean();						//zero out all arrays
	auto sequence = neural_mem.block_mem();	//grab the memory as an array

	//go through the memory array
	for(size_t i = 0; i < sequence.size() - 1; i++) {
		//set the parameter values of the particular block to either
		//full scale or zero
		uint16_t param_val = i & UINT16_MAX;

		//and make a throwaway block with the particular param vals
		sequence[i] = Neural_Memory::Hispeed_Block_t::mk_throwaway({param_val, param_val, param_val, param_val});
	}

	//and the last block should be a terminator
	sequence.back() = Neural_Memory::Hispeed_Block_t::mk_term();
	attach_memory(); //allow editing after loading our test pattern
}

//pseudo-random pattern with same-index write-back
void Neural_Mem_Manager::load_mem_pattern_3() {
	detach_memory(); 						//prevent editing while we're loading our test pattern
	neural_mem.clean();						//zero out all arrays
	auto sequence = neural_mem.block_mem();	//grab the memory as an array

	//go through the memory array
	uint16_t r = 1;
	for(size_t i = 0; i < sequence.size() - 1; i++) {
		//use an XOR shift to make a pseudorandom sequence
		r ^= r << 11;
		r ^= r >> 7;
		r ^= r << 3;

		//and make a block that writes the ADC values back to the corresponding indices of the sequence
		sequence[i] = Neural_Memory::Hispeed_Block_t::mk(	{r, r, r, r},
															{	Neural_Memory::ADC_Destination_t(i, 0, 0),
																Neural_Memory::ADC_Destination_t(i, 1, 0),
																Neural_Memory::ADC_Destination_t(i, 2, 0),
																Neural_Memory::ADC_Destination_t(i, 3, 0)	});
	}

	//and the last block should be a terminator
	sequence.back() = Neural_Memory::Hispeed_Block_t::mk_term();
	attach_memory(); //allow editing after loading our test pattern
}

//DC value with future channel write-back
void Neural_Mem_Manager::load_mem_pattern_4() {
	detach_memory(); 						//prevent editing while we're loading our test pattern
	neural_mem.clean();						//zero out all arrays
	auto sequence = neural_mem.block_mem();	//grab the memory as an array

	//go through half the memory array
	for(size_t i = 0; i < (sequence.size() - 1)/2; i++) {
		//compute the corresponding index into the second half
		size_t dest_addr = i + (sequence.size() - 1)/2;
		//and make a block that writes the ADC values to the second half of the array
		sequence[i] = Neural_Memory::Hispeed_Block_t::mk(	{8000, 24000, 40000, 56000},
															{	Neural_Memory::ADC_Destination_t(dest_addr, 0, 0),
																Neural_Memory::ADC_Destination_t(dest_addr, 1, 0),
																Neural_Memory::ADC_Destination_t(dest_addr, 2, 0),
																Neural_Memory::ADC_Destination_t(dest_addr, 3, 0)	});
	}

	//and the last block should be a terminator
	sequence.back() = Neural_Memory::Hispeed_Block_t::mk_term();
	attach_memory(); //allow editing after loading our test pattern
}



