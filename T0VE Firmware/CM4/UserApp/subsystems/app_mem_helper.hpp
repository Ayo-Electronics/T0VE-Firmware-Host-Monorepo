/*
 * app_mem_helper.hpp
 *
 *  Created on: Oct 28, 2025
 *      Author: govis
 *
 *  Use this file to help organize external memory
 */

#pragma once

#include "app_proctypes.hpp"
#include "app_shared_memory.h"	//so we can keep some of these shared structures accessible to both cores
#include "app_hal_dram.hpp"		//to initialize external memory
#include "app_utils.hpp"
#include "app_msc_if.hpp"		//so we can present this memory over USB
#include "app_msc_file.hpp"		//expose memory as different files

/*
 * in regards to hispeed block array execution, just return an underlying pointer to the start of the block array
 * no need for particularly high level of abstraction since it's literally just memory
 * and high-speed execution relies on direct memory access with minimal overhead
 */

// Macro to create constructor args for MSC_File from a std::span<T> and a string filename
#define MSC_FILE_INIT(span_obj, name_str) 																					\
	{																														\
		std::span<uint8_t, std::dynamic_extent>(reinterpret_cast<uint8_t*>((span_obj).data()), (span_obj).size_bytes()), 	\
		App_String<FS_Constants::FILENAME_MAX_LENGTH>{(name_str)}															\
	}

class Mem_Helper {
public:
	//==================================== PUBLIC TYPEDEFS ====================================

	//####################### HISPEED EXECUTION MEMORY ##########################
	//these structures are designed to live in DRAM
	/*
	 * ADC_Destination_t
	 *  \--> want to pack information about where to route an ADC conversion result
	 *  \--> We need to encode:
	 *  		- Which block address to dump it in
	 *  		- Which index to dump it in
	 *  		- If we just wanna throw the ADC value away
	 *
	 *  I'm proposing a structure that looks like:
	 *  	 [0..27] --> block index
	 *  	[28..29] --> sub index
	 *  	 	[30] --> throwaway flag	 (`1` if throwaway)
	 *  	 	[31] --> block valid flag (`1` if valid)
	 *
	 *	I'd likely implement this as a struct with methods rather than a union to speed up access
	 *		\--> methods will just be bit shifts
	 *
	 *  For simple fully-connected feed-forward networks, we can run without a sequence control counter
	 *  Essentially, we'd run the system until we hit an invalid block, where we'd disarm and stop
	 *  I'd like to be able to quickly check if a block is valid or not, likely going to be just checking if the destination is 0
	 */
	struct ADC_Destination_t {
		//some consts just to make reading/writing more consistent
		static constexpr uint32_t BLOCK_INDEX_MASK = 0x0FFF'FFFF;
		static constexpr size_t BLOCK_INDEX_SHIFT = 0;
		static constexpr uint32_t SUB_INDEX_MASK = 0x03;
		static constexpr size_t SUB_INDEX_SHIFT = 28;
		static constexpr uint32_t THROWAWAY_MASK = 0x01;
		static constexpr size_t THROWAWAY_SHIFT = 30;
		static constexpr uint32_t BLOCK_VALID_MASK = 0x01;
		static constexpr size_t BLOCK_VALID_SHFIT = 31;

		//just the data, store as uint32_t
		uint32_t dest_data;

		//construct with either data or fields, default construct to 0
		ADC_Destination_t(): dest_data(0) {}
		ADC_Destination_t(uint32_t _dest_data): dest_data(_dest_data) {}
		ADC_Destination_t(uint32_t block_index, uint32_t sub_index, uint32_t throwaway):
			dest_data( 	((block_index & BLOCK_INDEX_MASK) << BLOCK_INDEX_SHIFT) |
						((sub_index & SUB_INDEX_MASK) << SUB_INDEX_SHIFT) 		|
						((throwaway & THROWAWAY_MASK) << THROWAWAY_SHIFT)		|
						( BLOCK_VALID_MASK << BLOCK_VALID_SHFIT)					)
		{}

		//methods to operate on the data field
		__attribute__((always_inline)) inline uint32_t block_index() 	{ return ((dest_data >> BLOCK_INDEX_SHIFT) & BLOCK_INDEX_MASK); 	}
		__attribute__((always_inline)) inline uint32_t sub_index() 		{ return ((dest_data >> SUB_INDEX_SHIFT) & SUB_INDEX_MASK); 		}
		__attribute__((always_inline)) inline bool throwaway()			{ return ( dest_data & (THROWAWAY_MASK << THROWAWAY_SHIFT));		}
		__attribute__((always_inline)) inline bool valid()				{ return ( dest_data & (BLOCK_VALID_MASK << BLOCK_VALID_SHFIT));	}
	};

	/*
	 * Hispeed_Block_t
	 * Contains information relevant to execution of a block
	 * 	\--> `param_vals` contains the values to write to the DACs
	 * 	\--> `readback_destinations` contains information on where to store the ADC readings
	 * 	Using C-style arrays to minimize the std::array overhead (very slight performance bump)
	 */
	struct Hispeed_Block_t {
		uint16_t param_vals[4];
		ADC_Destination_t readback_destinations[4];

		//factory function
		//direct copy construction of C-arrays can be kinda weird, so just making it step-wise
		static Hispeed_Block_t mk(std::array<uint16_t, 4> _vals, std::array<ADC_Destination_t, 4> _dest) {
			Hispeed_Block_t block;
			std::copy(_vals.begin(), _vals.end(), block.param_vals); //param vals decays into a pointer
			std::copy(_dest.begin(), _dest.end(), block.readback_destinations);
			return block;
		}

		//special factory function that makes a block that throws away ADC value
		static Hispeed_Block_t mk_throwaway(std::array<uint16_t, 4> _vals) {
			//explicitly call ADC_Destination_t constructor
			return mk(_vals, {	ADC_Destination_t(0, 0, 1),
								ADC_Destination_t(0, 1, 1),
								ADC_Destination_t(0, 2, 1),
								ADC_Destination_t(0, 3, 1)	});
		}

		//special factory function that makes a terminating block
		static Hispeed_Block_t mk_term() { return mk({0}, {0});	}
	};

	//==================================== PUBLIC FUNCTIONS ======================================
	//construct with pointer to MSC interface
	//can be nullptr if we don't want to expose files over USB
	Mem_Helper(DRAM* _dram = nullptr, MSC_Interface* _msc_if = nullptr);

	/*
	 * Init function
	 *  - initializes the MSC class (if not null)
	 *  - initializes the external DRAM (if not null)
	 */
	void init();

	/*
	 * Use these functions to let the memory controller expose the files over the mass-storage interface
	 * `attach` exposes the files, `detach` removes the file
	 */
	void detach_files();
	void attach_files();

	/*
	 * Use these functions to transfer the inputs in the input array to the correct spots in the block memory
	 */
	void transfer_inputs();
	void transfer_outputs();

	/*
	 * Get access to the underlying DRAM
	 * Useful for the high-speed subsystem to perform direct memory access to the DRAM without any code overhead
	 */
	std::span<Hispeed_Block_t, std::dynamic_extent> block_mem();

	//don't allow copy construction or assignment operation - almost always accidental
	Mem_Helper(const Mem_Helper& other) = delete;
	void operator=(const Mem_Helper& other) = delete;

private:
	//own a pointer to the mass-storage class and a DRAM interface
	DRAM* dram;
	MSC_Interface* msc_if;

	//own a view into the DRAM
	//treat everything in DRAM as a hispeed block
	static constexpr size_t NUM_BLOCKS = NETWORK_SIZE/sizeof(Hispeed_Block_t);	//assumes padding of Hispeed_Block_t
	Hispeed_Block_t* const BLOCK_MEMORY_START = reinterpret_cast<Hispeed_Block_t*>(&SHARED_EXTMEM.NETWORK);
	std::span<Hispeed_Block_t> BLOCK_MEMORY = {BLOCK_MEMORY_START, NUM_BLOCKS};
	MSC_File BLOCK_MEM_FILE = MSC_FILE_INIT(BLOCK_MEMORY, "NEURAL_NETWORK_MEMORY.bin");


	//own a view into the shared AXI SRAM where network inputs will live
	//sanity check our inputs array sizing
	static_assert(sizeof(SHARED_FASTMEM.INPUTS[0]) == sizeof(uint16_t), "size mismatch between network inputs definition!");
	uint16_t* const NETWORK_INPUTS_START = reinterpret_cast<uint16_t*>(&SHARED_FASTMEM.INPUTS);
	std::span<uint16_t> NETWORK_INPUTS = {NETWORK_INPUTS_START, INPUTS_SIZE};
	MSC_File NETWORK_INPUTS_FILE = MSC_FILE_INIT(NETWORK_INPUTS, "NETWORK_INPUTS.bin");

	//and a view into shared AXI SRAM where input mappings will live
	//sanity check our input mapping sizing
	static_assert(sizeof(SHARED_FASTMEM.INPUT_MAPPING[0]) == sizeof(ADC_Destination_t), "size mismatch between network input mapping definition!");
	ADC_Destination_t* const NETWORK_INPUT_MAPPING_START = reinterpret_cast<ADC_Destination_t*>(&SHARED_FASTMEM.INPUT_MAPPING);
	std::span<ADC_Destination_t> NETWORK_INPUT_MAPPING = {NETWORK_INPUT_MAPPING_START, INPUTS_SIZE};
	MSC_File NETWORK_INPUT_MAPPING_FILE = MSC_FILE_INIT(NETWORK_INPUT_MAPPING, "NETWORK_INPUT_MAPPING.bin");

	//a view into the shared AXI SRAM where network outputs will live
	//sanity check our outputs array sizing
	static_assert(sizeof(SHARED_FASTMEM.OUTPUTS[0]) == sizeof(uint16_t), "size mismatch between network output definition!");
	uint16_t* const NETWORK_OUTPUTS_START = reinterpret_cast<uint16_t*>(&SHARED_FASTMEM.OUTPUTS);
	std::span<uint16_t> NETWORK_OUTPUTS = {NETWORK_OUTPUTS_START, OUTPUTS_SIZE};
	MSC_File NETWORK_OUTPUTS_FILE = MSC_FILE_INIT(NETWORK_OUTPUTS, "NETWORK_OUTPUTS.bin");

	//and a view into shared AXI SRAM where output mappings will live
	//sanity check our output mapping sizing
	static_assert(sizeof(SHARED_FASTMEM.OUTPUT_MAPPING[0]) == sizeof(ADC_Destination_t), "size mismatch between network output mapping definition!");
	ADC_Destination_t* const NETWORK_OUTPUT_MAPPING_START = reinterpret_cast<ADC_Destination_t*>(&SHARED_FASTMEM.OUTPUT_MAPPING);
	std::span<ADC_Destination_t> NETWORK_OUTPUT_MAPPING = {NETWORK_OUTPUT_MAPPING_START, INPUTS_SIZE};
	MSC_File NETWORK_OUTPUT_MAPPING_FILE = MSC_FILE_INIT(NETWORK_OUTPUT_MAPPING, "NETWORK_OUTPUT_MAPPING.bin");
};
