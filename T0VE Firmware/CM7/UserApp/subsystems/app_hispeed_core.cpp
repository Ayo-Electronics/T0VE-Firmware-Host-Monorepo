/*
 * app_hispeed_core.cpp
 *
 *  Created on: Oct 27, 2025
 *      Author: govis
 */

#include "app_hispeed_core.hpp"


/*
 * Original Hispeed core replicated below
 */

#pragma GCC push_options
#pragma GCC optimize ("Ofast,inline-functions")

//__attribute__((section(".ITCMRAM_Section"))) //PLACE THIS FUNCTION IN ITCM FOR SPEED
//void Hispeed_Subsystem::do_hispeed_arm_fire() {
//	//NOTE: ENTERING THIS FUNCTION ASSUMING TIA/SOA LINES ARE CONFIGURED AS THE USER WANTS!
//	//check if we actually wanna arm/fire the hispeed subsystem
//	//return now if we don't
//	if(!command_hispeed_arm_fire_request) return;
//
//	//signal that we're in the armed state
//	status_hispeed_armed = true;
//
//	//then deschedule our main thread update function and call a scheduler update
//	//lets us tell other subsystems that we're in the armed state
//	check_state_update_task.deschedule();
//	Scheduler::update();
//
//	//create some index/flow control variables
//	size_t current_block_index = 0;
//
//	//active block is the first from the list
//	//place it on a 32 byte boundary for friendly caching and retrieval
//	__attribute__((aligned(32))) Hispeed_Block_t active_block = block_sequence[current_block_index];
//	uint16_t* __restrict adc_destination_addresses[4] = { 	&throwaway_block.param_vals[0],		//throw the first set of ADC values away
//															&throwaway_block.param_vals[1],		//using c-style arrays for potentially more performance
//															&throwaway_block.param_vals[2],
//															&throwaway_block.param_vals[3] 	};
//
//	//create a variable that reports how we exited the loop
//	//default to a regular exit
//	Loop_Exit_Status_t exit_status = Loop_Exit_Status_t::LOOP_EXIT_OK;
//
//	//arm our hardware channels
//	//puts the chip select I/Os in alternate mode under timer control
//	//and enables the one-shot triggered timers that control them
//	CHANNEL_0.arm();
//	CHANNEL_1.arm();
//	CHANNEL_2.arm();
//	CHANNEL_3.arm();
//
//	//##### ENTERING CRITICAL SECTION #####
//	//don't want to be interrupted with bogus context switches
//	//disable interrupts
//	//NOTE: THIS HAS A BYPRODUCT OF STOPPING SYSTICK!
//	//HAVE TO USE A DIFFERENT TIMER FOR TIMEOUTS
//	__disable_irq();
//
//	//make sure we enter the hot loop knowing all of our SPI devices are ready to receive data
//	//checking now because we write data directly after the chip select goes low--done for performance reasons
//	//we then check whether the SPI peripherals are good for future writes on the "cold" side of the loop
//	//while the analog circuits are settling
//	while(!CHANNEL_0.device_pair.READY_WRITE());
//	while(!CHANNEL_1.device_pair.READY_WRITE());
//	while(!CHANNEL_2.device_pair.READY_WRITE());
//	while(!CHANNEL_3.device_pair.READY_WRITE());
//
//	//assert our node ready
//	//and wait for all cards to be ready
//	//do this measurement with DWT since systick doesn't work without interrupts
//	Tick::init_cycles();
//	Tick::start_cycles();
//	Tick::reset_cycles();
//	multicard_interface.node_is_ready();
//	while(!multicard_interface.get_all_ready()) {
//		if(Tick::get_cycles() > TIMEOUT_TICKS) {
//			exit_status = Loop_Exit_Status_t::LOOP_EXIT_ERR_READY;
//			goto Afterloop;
//		}
//	}
//
//	//clear any pending triggers on the syncin timer
//	multicard_interface.reset_sync_triggered();
//
//	//now we enable our SYNCOUT and SYNCIN timers
//	//and enter the high speed loop after this
//	multicard_interface.enable_sync_timer();
//
//	while(true) {
//		//start by waiting for DAC_CS to go low
//		//	\--> can read the state of the I/O line even when in alternate mode
//		//	\--> prefer prefer this, because DAC_CS follows immediately after sync RISING EDGE
//		//	\--> can be edge cases with firmware-based rising edge detection that kinda scares me
//		//if we have the option, this would be a good time for `__builtin_expect()` for branch prediction (bias toward exiting loop)
//		//TESTING--starting and stopping the cycle counter TODO: remove in prod
//		Tick::start_cycles();
//		Tick::reset_cycles(); //resetting our DWT to check sync timeout
//		while(__builtin_expect(	!multicard_interface.get_sync_triggered() &&	//wait for sync interface timer to be triggered
//								(Tick::get_cycles() < TIMEOUT_TICKS),			//AND make sure we don't time out
//				false));
//		Tick::stop_cycles();
//
//		//ALL SPI buses should be ready for data
//		//dump the parameters from the current block into the DACs via SPI bus
//		CHANNEL_0.device_pair.RAW_WRITE(active_block.param_vals[0]);
//		CHANNEL_1.device_pair.RAW_WRITE(active_block.param_vals[1]);
//		CHANNEL_2.device_pair.RAW_WRITE(active_block.param_vals[2]);
//		CHANNEL_3.device_pair.RAW_WRITE(active_block.param_vals[3]);
//
//		//clear the triggered flag for the syncin timer
//		multicard_interface.reset_sync_triggered();
//
//		//this is a good time to check our exit conditions
//		//checking here rather than in the CS wait hot loop reduces latency to the SPI writes
//		if(Tick::get_cycles() >= TIMEOUT_TICKS) {
//			//effectively checking if we timed outta our chip select wait
//			//says we were waiting for a sync signal all this time
//			exit_status = Loop_Exit_Status_t::LOOP_EXIT_ERR_SYNC_TIMEOUT;
//			break;
//		}
//		else if(!onboard_power_monitor.get_immediate_power_status()) {
//			//checking whether power is good --> reading the PGOOD pin directly
//			//if it's bad, leave the loop
//			exit_status = Loop_Exit_Status_t::LOOP_EXIT_ERR_POWER;
//			break;
//		}
//		else if(!multicard_interface.get_all_ready()) {
//			//checking whether all nodes are still good for execution
//			//if they aren't leave the loop
//			exit_status = Loop_Exit_Status_t::LOOP_EXIT_ERR_READY;
//			break;
//		}
//
//		//while we're waiting, for the transaction to complete prefetch the next block in the block sequence
//		//should speed up our fetch of the next block
//		__builtin_prefetch(&block_sequence[current_block_index + 1]);
//
//		//and now actually wait for the transactions to complete
//		//checking the first channel should be sufficient, since all channels are basically the same
//		//if we have the option, this would be a good time for `__builtin_expect()` for branch prediction (bias toward exiting loop)
//		while(__builtin_expect(!CHANNEL_0.device_pair.READY_READ(), false));
//
//		//all SPI buses should be ready for reading
//		//dump the ADC data directly into the memory addresses we calculated from the previous iteration
//		//	\--> on the first iteration, dump them into the throwaway
//		*adc_destination_addresses[0] = CHANNEL_0.device_pair.RAW_READ();
//		*adc_destination_addresses[1] = CHANNEL_1.device_pair.RAW_READ();
//		*adc_destination_addresses[2] = CHANNEL_2.device_pair.RAW_READ();
//		*adc_destination_addresses[3] = CHANNEL_3.device_pair.RAW_READ();
//
//		//the last thing we'll do before fetching our next block
//		//is to calculate the memory addresses we want to dump the results of this current execution
//		//do so in this loop (asking the compilier to unroll this loop for speed)
//		#pragma GCC unroll 4
//		for(size_t i = 0; i < 4; i++){
//			//grab information regarding the destination that we'd like to place the particular ADC channel
//			//will contain a block index, a specific channel index (0-4), and whether we should just discard the ADC value
//			//reference, so we don't spend extra instructions copying
//			ADC_Destination_t& dest_info = active_block.readback_destinations[i];
//
//			//check if the particular ADC value is a throwaway or not
//			bool is_throwaway = dest_info.throwaway();
//
//			//if it is, dump the ADC reading into the throwaway block
//			//and if not, dump the ADC reading into a particular block in the sequence
//			Hispeed_Block_t& dest_block = is_throwaway ? throwaway_block : block_sequence[dest_info.block_index()];
//
//			//then our final address is the channel index provided within the destination information
//			adc_destination_addresses[i] = &dest_block.param_vals[dest_info.sub_index()];
//		}
//
//		//now we can finally fetch the next block in our sequence from memory
//		current_block_index++;
//		active_block = block_sequence[current_block_index];
//
//		//we'll then check if the block for the next iteration is a valid block or not
//		//can be done by checking whether any of the destination addresses are valid or not
//		//implicitly exiting the loop with okay status
//		//if we have the option, this would be a good time for `__builtin_expect()` for branch prediction (bias toward exiting loop)
//		if(!active_block.readback_destinations[0].valid()) break; //current_block_index = 0;
//
//		//and last thing, make sure the SPI peripherals are ready for a write for the next iteration
//		//checking/stalling here rather than after CS goes low reduces latency to the SPI writes
//		//checking just the first one should be sufficient, since they're all the configured identically
//		//if we have the option, this would be a good time for `__builtin_expect()` for branch prediction (bias toward exiting loop)
//		while(__builtin_expect(!CHANNEL_0.device_pair.READY_WRITE(), false));
//	}
//
//	//a `goto` label to skip the loop if we timed out waiting for the ALL_READY line to go high
//	//most straightforward way to skip the loop
//	Afterloop:
//
//	//Diagnostic information
//	//If we read the cycle counter now, we should get the number of cycles we were waiting
//	//in our previous loop iteration --> shows us idle time/how much overhead we have
//	volatile uint32_t cycles = Tick::get_cycles();
//	(void)cycles;
//
//	//#### UPON EXIT ####
//	//signal node no longer ready
//	multicard_interface.node_not_ready();
//
//	//disable the sync timer
//	multicard_interface.disable_sync_timer();
//
//	//##### EXITING CRITICAL SECTION #####
//	//can allow interrupts again
//	__enable_irq();
//
//	//disarm the hardware
//	CHANNEL_0.disarm();
//	CHANNEL_1.disarm();
//	CHANNEL_2.disarm();
//	CHANNEL_3.disarm();
//
//	//set the appropriate flag state variable depending on the exit state
//	if(exit_status == Loop_Exit_Status_t::LOOP_EXIT_ERR_SYNC_TIMEOUT)
//		status_hispeed_arm_flag_err_sync_timeout = true;
//	else if(exit_status == Loop_Exit_Status_t::LOOP_EXIT_ERR_POWER)
//		status_hispeed_arm_flag_err_pwr = true;
//	else if(exit_status == Loop_Exit_Status_t::LOOP_EXIT_ERR_READY)
//		status_hispeed_arm_flag_err_ready = true;
//	else
//		status_hispeed_arm_flag_complete = true;
//
//	//no longer in the armed state
//	status_hispeed_armed = false;
//
//	//and finally, acknowledge the arm/fire request
//	//and reschedule our main thread update function
//	command_hispeed_arm_fire_request.acknowledge_reset();
//	check_state_update_task.schedule_interval_ms(BIND_CALLBACK(this, check_state_update_run_esm), Scheduler::INTERVAL_EVERY_ITERATION);
//}
//
//#pragma GCC pop_options
