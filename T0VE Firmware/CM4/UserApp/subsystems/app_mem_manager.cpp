/*
 * app_mem_manager.cpp
 *
 *  Created on: Nov 4, 2025
 *      Author: govis
 */

#include "app_mem_manager.hpp"
#include "app_shared_memory.h" //network size

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

	//attach memory, check the IO mappings, and load a pattern if we want at startup
	check_attach_memory();
	check_io_mappings();
	check_load_mem_pattern();

	//and start our state update task
	check_state_update_task.schedule_interval_ms(	BIND_CALLBACK(this, check_state_update),
													Scheduler::INTERVAL_EVERY_ITERATION);
}

//============= PRIVATE FUNCTIONS ==============
void Neural_Mem_Manager::check_state_update() {
	//check if we want to attach/detach our memory
	if(command_nmemmanager_attach_memory.check()) {
		check_attach_memory();
	}

	//check if we wanna analyze our discovered input/output size
	if(command_nmemmanager_check_io_size.check()) {
		check_io_mappings();
	}

	//and if we want to load some test patterns
	if(command_nmemmanager_load_test_pattern.check()) {
		check_load_mem_pattern();
	}
}

void Neural_Mem_Manager::check_io_mappings() {
	//don't run the test if we cleared the flag early
	if(!command_nmemmanager_check_io_size.read()) return;

	//grab the input mapping
	auto imap = neural_mem.input_mapping();

	//go through these inputs, and find the first invalid index
	//this is where the input transfer will stop; corresponds with the input size
	size_t detect_size;
	for(detect_size = 0; detect_size < imap.size(); detect_size++) {
		auto& dest = imap[detect_size];
		if(!dest.valid()) break;
	}
	status_nmemmanager_detected_input_size.publish(detect_size);

	//do the exact same thing with the output mappings
	//grab the output mapping
	auto omap = neural_mem.output_mapping();

	//go through these inputs, and find the first invalid index
	//this is where the input transfer will stop; corresponds with the input size
	for(detect_size = 0; detect_size < omap.size(); detect_size++) {
		auto& dest = omap[detect_size];
		if(!dest.valid()) break;
	}
	status_nmemmanager_detected_output_size.publish(detect_size);

	//and acknowledge our command flag
	command_nmemmanager_check_io_size.acknowledge_reset();
}

//checks the command variable, attaches/detaches memory accordingly
void Neural_Mem_Manager::check_attach_memory() {
	if(command_nmemmanager_attach_memory.read()) 	attach_memory();
	else 											detach_memory();
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
//check to see if we need to load a memory test pattern
void Neural_Mem_Manager::check_load_mem_pattern() {
	//load the test pattern accordingly
	auto test_pattern = command_nmemmanager_load_test_pattern.read();
	if(test_pattern == 1) load_mem_pattern_1();
	if(test_pattern == 2) load_mem_pattern_2();
	if(test_pattern == 3) load_mem_pattern_3();
	if(test_pattern == 4) load_mem_pattern_4();
	if(test_pattern == 5) load_mem_pattern_5();
	if(test_pattern == 6) load_mem_pattern_6();
	if(test_pattern == 7) load_mem_pattern_7();

	//and acknowledge the test pattern load flag
	command_nmemmanager_load_test_pattern.acknowledge_reset();
}

//square wave between 0 and full, discard outputs
void Neural_Mem_Manager::load_mem_pattern_1() {
	detach_memory(); 						//prevent editing while we're loading our test pattern
	neural_mem.clean();						//zero out all arrays
	auto sequence = neural_mem.block_mem();	//grab the memory as an array

	//go through the memory array
	for(size_t i = 0; i < sequence.size() - 1; i++) {
		//set the parameter values of the particular block to either
		//full scale or zero
		uint16_t param_val = (i % 2 == 0) ? 16000 : 0; //corresponds to 1.25mA; 2.5V with nominal TIA loopback

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

void Neural_Mem_Manager::load_mem_pattern_5() {
	detach_memory();	//prevent editing while we're loading our test pattern
	neural_mem.clean();	//start fresh with the arrays

	//start going through the inputs, assign them according to their index
	auto memsize = neural_mem.block_mem().size(); //used later
	auto outputs = neural_mem.outputs(); //used later
	auto inputs = neural_mem.inputs();
	for(size_t i = 0; i < inputs.size(); i++) inputs[i] = i & 0xFFFF;

	//the sequence will be just a single terminator block
	//basically skips network execution entirely
	auto sequence = neural_mem.block_mem();
	sequence[0] = Neural_Memory::Hispeed_Block_t::mk_term();

	//now make the mapping, the core of the test
	//place the blocks in basically pseudorandom spots
	//but ensuring no overlap - just gonna use a constant stride that is coprime with our wrap-around
	static const size_t STRIDE = 8191;	//unlikely we'll have a common multiple
	static const size_t STRIDE_MOD = (memsize % STRIDE) == 0 ? memsize - 1 : memsize; //but just in case
	auto in_mapping = neural_mem.input_mapping();
	auto out_mapping = neural_mem.output_mapping();
	size_t num_io = min(in_mapping.size(), out_mapping.size());
	for(size_t i = 0; i < num_io; i++) {
		size_t dest_addr = (i * STRIDE) % STRIDE_MOD; //constant stride destination
		auto dest = Neural_Memory::ADC_Destination_t(dest_addr, i%4, 0); //rotating sub-index
		in_mapping[i] = dest;
		out_mapping[i] = dest;
	}

}

//square pattern on 0, 1VDC on others, same index outputs
void Neural_Mem_Manager::load_mem_pattern_6() {
	//set up some constants regarding the ramp amplitude and the maximum DAC value
	static const uint16_t CONST_DAC_VAL = 16000;	//corresponds to 1VDC
	static const uint16_t SQUARE_MIN = 1000;		//results in TIA readback of ~2,500
	static const uint16_t SQUARE_MAX = 24000;		//results in TIA readback of ~60,000

	detach_memory(); 						//prevent editing while we're loading our test pattern
	neural_mem.clean();						//zero out all arrays
	auto sequence = neural_mem.block_mem();	//grab the memory as an array

	//go through the memory array
	for(size_t i = 0; i < sequence.size() - 1; i++) {
		//set the channel 1 value to be a sawtooth waveform with peak given above
		uint16_t param_val = (i % 2 == 0) ? SQUARE_MIN : SQUARE_MAX;

		//and make a block with the particular param vals that drops the ADC val corresponding to the command index
		sequence[i] = Neural_Memory::Hispeed_Block_t::mk(	{param_val, CONST_DAC_VAL, CONST_DAC_VAL, CONST_DAC_VAL},
															{	Neural_Memory::ADC_Destination_t(i, 0, 0),
																Neural_Memory::ADC_Destination_t(i, 1, 0),
																Neural_Memory::ADC_Destination_t(i, 2, 0),
																Neural_Memory::ADC_Destination_t(i, 3, 0)	});
	}

	//and the last block should be a terminator
	sequence.back() = Neural_Memory::Hispeed_Block_t::mk_term();
	attach_memory(); //allow editing after loading our test pattern
}

//sawtooth pattern on 0, 1VDC on others, same index outputs
void Neural_Mem_Manager::load_mem_pattern_7() {
	//set up some constants regarding the ramp amplitude and the maximum DAC value
	static const uint16_t CONST_DAC_VAL = 16000;	//corresponds to 1VDC
	static const uint16_t RAMP_PEAK = 24000;		//results in TIA readback of ~60,000 peak

	detach_memory(); 						//prevent editing while we're loading our test pattern
	neural_mem.clean();						//zero out all arrays
	auto sequence = neural_mem.block_mem();	//grab the memory as an array

	//go through the memory array
	for(size_t i = 0; i < sequence.size() - 1; i++) {
		//set the channel 1 value to be a sawtooth waveform with peak given above
		uint16_t param_val = i % RAMP_PEAK;

		//and make a block with the particular param vals that drops the ADC val corresponding to the command index
		sequence[i] = Neural_Memory::Hispeed_Block_t::mk(	{param_val, CONST_DAC_VAL, CONST_DAC_VAL, CONST_DAC_VAL},
															{	Neural_Memory::ADC_Destination_t(i, 0, 0),
																Neural_Memory::ADC_Destination_t(i, 1, 0),
																Neural_Memory::ADC_Destination_t(i, 2, 0),
																Neural_Memory::ADC_Destination_t(i, 3, 0)	});
	}

	//and the last block should be a terminator
	sequence.back() = Neural_Memory::Hispeed_Block_t::mk_term();
	attach_memory(); //allow editing after loading our test pattern
}


