/*
 * app_hispeed_subsys.cpp
 *
 *  Created on: Aug 25, 2025
 *      Author: govis
 */

#include "app_hispeed_subsys.hpp"


//==================== CONSTRUCTOR =====================
Hispeed_Subsystem::Hispeed_Subsystem(	Hispeed_Channel_Hardware_t ch0,
										Hispeed_Channel_Hardware_t ch1,
										Hispeed_Channel_Hardware_t ch2,
										Hispeed_Channel_Hardware_t ch3,
										DRAM::DRAM_Hardware_Channel& _block_memory_hw,
										Power_Monitor& _onboard_power_monitor,
										Multicard_Info& _multicard_interface	):
	//initialize each channel with the hardware provided
	CHANNEL_0(ch0), CHANNEL_1(ch1),	CHANNEL_2(ch2),	CHANNEL_3(ch3),

	//save the power monitor and DRAM
	block_memory(_block_memory_hw),
	onboard_power_monitor(_onboard_power_monitor),
	multicard_interface(_multicard_interface),

	//then construct the state machine states
	hispeed_state_INACTIVE(BIND_CALLBACK(this, deactivate), {}, {}),
	hispeed_state_ACTIVE(BIND_CALLBACK(this, activate), {}, {}),
	hispeed_state_ARM_FIRE(BIND_CALLBACK(this, do_hispeed_arm_fire), {}, {}),

	//and finally the state machine itself
	//enter into the inactive state
	hispeed_esm(&hispeed_state_INACTIVE)
{
	//link the state machine transitions here
	hispeed_state_INACTIVE.attach_state_transitions(hispeed_trans_FROM_INACTIVE);
	hispeed_state_ACTIVE.attach_state_transitions(hispeed_trans_FROM_ACTIVE);
	hispeed_state_ARM_FIRE.attach_state_transitions(hispeed_trans_FROM_ARM_FIRE);

	//and link the power monitor power good subscription here
	status_onboard_pgood = onboard_power_monitor.subscribe_status_debounced_power();

	//and point block sequence to the start of DRAM
	block_sequence = reinterpret_cast<Hispeed_Block_t*>(block_memory.start());
}

//===================== INITIALIZATION FUNCTIONS ====================
void Hispeed_Subsystem::init() {
	//initialize all of our high-speed channels with the helper functions
	CHANNEL_0.init();
	CHANNEL_1.init();
	CHANNEL_2.init();
	CHANNEL_3.init();

	//configure the chip select timing for all the channels
	CHANNEL_0.configure_timing(CS_DAC_LOWTIME, CS_ADC_LOWTIME);
	CHANNEL_1.configure_timing(CS_DAC_LOWTIME, CS_ADC_LOWTIME);
	CHANNEL_2.configure_timing(CS_DAC_LOWTIME, CS_ADC_LOWTIME);
	CHANNEL_3.configure_timing(CS_DAC_LOWTIME, CS_ADC_LOWTIME);

	//initialize the DRAM
	block_memory.init();
	block_memory.test();

	//and configure the SYNCOUT timer with our desired frequency and duty cycle
	multicard_interface.configure_sync_timer(SYNC_FREQUENCY, SYNC_DUTY);

	//load the SDRAM with the test sequence upon initialization if desired
	do_load_sdram_test_sequence();
	//NOT going to update the other hardware with any of our default states
	//we want to enter the system in a safe configuration--enter with defaults from `deactivate`

	//schedule the task that runs our state machine and checks some "asynchronous" commands
	check_state_update_task.schedule_interval_ms(BIND_CALLBACK(this, check_state_update_run_esm), Scheduler::INTERVAL_EVERY_ITERATION);
}

//PRIVATE function
void Hispeed_Subsystem::activate() {
	//reset the state variables
	command_hispeed_SOA_DAC_drive.acknowledge_reset();
	status_hispeed_TIA_ADC_readback.publish({0,0,0,0});
	command_hispeed_SOA_enable.acknowledge_reset();
	command_hispeed_TIA_enable.acknowledge_reset();
	command_hispeed_arm_fire_request.acknowledge_reset();

	//update the tia/soa GPIOs to their new reset state
	do_soa_gpio_control({0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}); //will disable all the SOAs for sure
	do_tia_gpio_control();

	//activate the hardware
	CHANNEL_0.activate();
	CHANNEL_1.activate();
	CHANNEL_2.activate();
	CHANNEL_3.activate();

	//schedule the thread functions
	hispeed_pilot_task.schedule_interval_ms(BIND_CALLBACK(this, do_hispeed_pilot), PILOT_TASK_PERIOD_MS);
}

//PRIVATE function
void Hispeed_Subsystem::deactivate() {
	//deschedule the thread functions
	hispeed_pilot_task.deschedule();

	//deactivate the hardware
	CHANNEL_0.deactivate();
	CHANNEL_1.deactivate();
	CHANNEL_2.deactivate();
	CHANNEL_3.deactivate();

	//reset the state variables
	command_hispeed_SOA_DAC_drive.acknowledge_reset();
	status_hispeed_TIA_ADC_readback.publish({0,0,0,0});
	command_hispeed_SOA_enable.acknowledge_reset();
	command_hispeed_TIA_enable.acknowledge_reset();
	command_hispeed_arm_fire_request.acknowledge_reset();

	//update the tia/soa GPIOs to their new reset state
	do_soa_gpio_control({0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}); //will disable all the SOAs for sure
	do_tia_gpio_control();
}

//===================== STATE VARIABLE GETTER/SETTER ===================


//===================== THREAD FUNCTIONS ======================
void Hispeed_Subsystem::check_state_update_run_esm() {
	//regarding state updates, just need to manage loading an SDRAM test sequence
	//rest of our state updates will be handled in `do_hispeed_pilot()`
	//which is started/stopped depending on state
	if(command_hispeed_sdram_load_test_sequence.check())
		do_load_sdram_test_sequence();

	//otherwise just run our extended state machine
	hispeed_esm.RUN_ESM();
}

void Hispeed_Subsystem::do_hispeed_pilot() {
	//NOTE: ADC values will need at least 1 period of the pilot update
	//in order to reflect the updated DAC values
	//i.e. correct ADC results for the currently written DAC values will be ready by *next* call to this function

	//creating a temporary array of all channels
	//makes performing this operation a little easier
	std::array<Hispeed_Channel_t*, 4> CHANNELn = {&CHANNEL_0, &CHANNEL_1, &CHANNEL_2, &CHANNEL_3};

	//write the commanded dac values and read the adc values to all channels
	//atomically copy the commanded DAC values to a temporary variable
	std::array<uint16_t, 4> dac_vals = command_hispeed_SOA_DAC_drive.read();
	std::array<uint16_t, 4> adc_vals;
	for(size_t i = 0; i < 4; i++)
		adc_vals[i] = CHANNELn[i]->device_pair.transfer(dac_vals[i]);

	//then store the adc values into the state variable
	status_hispeed_TIA_ADC_readback.publish(adc_vals);

	//if we have an update to TIA enable states
	if(command_hispeed_TIA_enable.check())
		do_tia_gpio_control();

	//if we have an update to SOA enable states
	//very deliberately running at the end of the pilot task
	if(command_hispeed_SOA_enable.check())
		//pass it the values we've actually written to our DAC
		//in the improbable event that the commanded DAC values have been updated between now and then
		do_soa_gpio_control(dac_vals);
}

void Hispeed_Subsystem::do_tia_gpio_control() {
	//read the state variable atomically
	std::array<bool, 4> tia_en_states = command_hispeed_TIA_enable.read();

	//and enable the TIA on the corresponding hardware channels if desired
	//do this via this loop
	std::array<Hispeed_Channel_t*, 4> CHANNELn = {&CHANNEL_0, &CHANNEL_1, &CHANNEL_2, &CHANNEL_3};
	for(size_t i = 0; i < 4; i++) {
		if(tia_en_states[i])
			CHANNELn[i]->tia_en.set();
		else
			CHANNELn[i]->tia_en.clear();
	}
}

//pass in the actual state of the DACs
//makes sure that the DACs are *truly* disabled when assessing SOA enable safety
void Hispeed_Subsystem::do_soa_gpio_control(std::array<uint16_t, 4> dac_drives) {
	//read the state variable atomically
	std::array<bool, 4> soa_en_states = command_hispeed_SOA_enable.read();

	//enable the SOA on the corresponding hardware channels if safety check passes
	//safety check being that the corresponding DAC drive is set to 0
	//do this via this loop
	std::array<bool, 4> actual_soa_en_states; //save what state we actually wrote the GPIOs to
	std::array<Hispeed_Channel_t*, 4> CHANNELn = {&CHANNEL_0, &CHANNEL_1, &CHANNEL_2, &CHANNEL_3};
	for(size_t i = 0; i < 4; i++) {
		if(soa_en_states[i] && (dac_drives[i] == 0)) {
			CHANNELn[i]->soa_en.set();
			actual_soa_en_states[i] = true;
		}
		else {
			CHANNELn[i]->soa_en.clear();
			actual_soa_en_states[i] = false;
		}
	}

	//update the command state variable to the values we actually wrote
	command_hispeed_SOA_enable.acknowledge_reset(actual_soa_en_states);
}

//===================== and MAIN HIGH-SPEED NETWORK EXECUTION FUNCTION ====================
#pragma GCC push_options
#pragma GCC optimize ("Ofast,inline-functions")

__attribute__((section(".ITCMRAM_Section"))) //PLACE THIS FUNCTION IN ITCM FOR SPEED
void Hispeed_Subsystem::do_hispeed_arm_fire() {
	//NOTE: ENTERING THIS FUNCTION ASSUMING TIA/SOA LINES ARE CONFIGURED AS THE USER WANTS!
	//check if we actually wanna arm/fire the hispeed subsystem
	//return now if we don't
	if(!command_hispeed_arm_fire_request.read()) return;

	//signal that we're in the armed state
	status_hispeed_armed.publish(true);

	//then deschedule our main thread update function and call a scheduler update
	//lets us tell other subsystems that we're in the armed state
	check_state_update_task.deschedule();
	Scheduler::update();

	//create some index/flow control variables
	size_t current_block_index = 0;

	//active block is the first from the list
	//place it on a 32 byte boundary for friendly caching and retrieval
	__attribute__((aligned(32))) Hispeed_Block_t active_block = block_sequence[current_block_index];
	uint16_t* __restrict adc_destination_addresses[4] = { 	&throwaway_block.param_vals[0],		//throw the first set of ADC values away
															&throwaway_block.param_vals[1],		//using c-style arrays for potentially more performance
															&throwaway_block.param_vals[2],
															&throwaway_block.param_vals[3] 	};

	//create a variable that reports how we exited the loop
	//default to a regular exit
	Loop_Exit_Status_t exit_status = Loop_Exit_Status_t::LOOP_EXIT_OK;

	//arm our hardware channels
	//puts the chip select I/Os in alternate mode under timer control
	//and enables the one-shot triggered timers that control them
	CHANNEL_0.arm();
	CHANNEL_1.arm();
	CHANNEL_2.arm();
	CHANNEL_3.arm();

	//##### ENTERING CRITICAL SECTION #####
	//don't want to be interrupted with bogus context switches
	//disable interrupts
	//NOTE: THIS HAS A BYPRODUCT OF STOPPING SYSTICK!
	//HAVE TO USE A DIFFERENT TIMER FOR TIMEOUTS
	__disable_irq();

	//make sure we enter the hot loop knowing all of our SPI devices are ready to receive data
	//checking now because we write data directly after the chip select goes low--done for performance reasons
	//we then check whether the SPI peripherals are good for future writes on the "cold" side of the loop
	//while the analog circuits are settling
	while(!CHANNEL_0.device_pair.READY_WRITE());
	while(!CHANNEL_1.device_pair.READY_WRITE());
	while(!CHANNEL_2.device_pair.READY_WRITE());
	while(!CHANNEL_3.device_pair.READY_WRITE());

	//assert our node ready
	//and wait for all cards to be ready
	//do this measurement with DWT since systick doesn't work without interrupts
	Tick::init_cycles();
	Tick::start_cycles();
	Tick::reset_cycles();
	multicard_interface.node_is_ready();
	while(!multicard_interface.get_all_ready()) {
		if(Tick::get_cycles() > TIMEOUT_TICKS) {
			exit_status = Loop_Exit_Status_t::LOOP_EXIT_ERR_READY;
			goto Afterloop;
		}
	}

	//clear any pending triggers on the syncin timer
	multicard_interface.reset_sync_triggered();

	//now we enable our SYNCOUT and SYNCIN timers
	//and enter the high speed loop after this
	multicard_interface.enable_sync_timer();

	while(true) {
		//start by waiting for DAC_CS to go low
		//	\--> can read the state of the I/O line even when in alternate mode
		//	\--> prefer prefer this, because DAC_CS follows immediately after sync RISING EDGE
		//	\--> can be edge cases with firmware-based rising edge detection that kinda scares me
		//if we have the option, this would be a good time for `__builtin_expect()` for branch prediction (bias toward exiting loop)
		//TESTING--starting and stopping the cycle counter TODO: remove in prod
		Tick::start_cycles();
		Tick::reset_cycles(); //resetting our DWT to check sync timeout
		while(__builtin_expect(	!multicard_interface.get_sync_triggered() &&	//wait for sync interface timer to be triggered
								(Tick::get_cycles() < TIMEOUT_TICKS),			//AND make sure we don't time out
				false));
		Tick::stop_cycles();

		//ALL SPI buses should be ready for data
		//dump the parameters from the current block into the DACs via SPI bus
		CHANNEL_0.device_pair.RAW_WRITE(active_block.param_vals[0]);
		CHANNEL_1.device_pair.RAW_WRITE(active_block.param_vals[1]);
		CHANNEL_2.device_pair.RAW_WRITE(active_block.param_vals[2]);
		CHANNEL_3.device_pair.RAW_WRITE(active_block.param_vals[3]);

		//clear the triggered flag for the syncin timer
		multicard_interface.reset_sync_triggered();

		//this is a good time to check our exit conditions
		//checking here rather than in the CS wait hot loop reduces latency to the SPI writes
		if(Tick::get_cycles() >= TIMEOUT_TICKS) {
			//effectively checking if we timed outta our chip select wait
			//says we were waiting for a sync signal all this time
			exit_status = Loop_Exit_Status_t::LOOP_EXIT_ERR_SYNC_TIMEOUT;
			break;
		}
		else if(!onboard_power_monitor.get_immediate_power_status()) {
			//checking whether power is good --> reading the PGOOD pin directly
			//if it's bad, leave the loop
			exit_status = Loop_Exit_Status_t::LOOP_EXIT_ERR_POWER;
			break;
		}
		else if(!multicard_interface.get_all_ready()) {
			//checking whether all nodes are still good for execution
			//if they aren't leave the loop
			exit_status = Loop_Exit_Status_t::LOOP_EXIT_ERR_READY;
			break;
		}

		//while we're waiting, for the transaction to complete prefetch the next block in the block sequence
		//should speed up our fetch of the next block
		__builtin_prefetch(&block_sequence[current_block_index + 1]);

		//and now actually wait for the transactions to complete
		//checking the first channel should be sufficient, since all channels are basically the same
		//if we have the option, this would be a good time for `__builtin_expect()` for branch prediction (bias toward exiting loop)
		while(__builtin_expect(!CHANNEL_0.device_pair.READY_READ(), false));

		//all SPI buses should be ready for reading
		//dump the ADC data directly into the memory addresses we calculated from the previous iteration
		//	\--> on the first iteration, dump them into the throwaway
		*adc_destination_addresses[0] = CHANNEL_0.device_pair.RAW_READ();
		*adc_destination_addresses[1] = CHANNEL_1.device_pair.RAW_READ();
		*adc_destination_addresses[2] = CHANNEL_2.device_pair.RAW_READ();
		*adc_destination_addresses[3] = CHANNEL_3.device_pair.RAW_READ();

		//the last thing we'll do before fetching our next block
		//is to calculate the memory addresses we want to dump the results of this current execution
		//do so in this loop (asking the compilier to unroll this loop for speed)
		#pragma GCC unroll 4
		for(size_t i = 0; i < 4; i++){
			//grab information regarding the destination that we'd like to place the particular ADC channel
			//will contain a block index, a specific channel index (0-4), and whether we should just discard the ADC value
			//reference, so we don't spend extra instructions copying
			ADC_Destination_t& dest_info = active_block.readback_destinations[i];

			//check if the particular ADC value is a throwaway or not
			bool is_throwaway = dest_info.throwaway();

			//if it is, dump the ADC reading into the throwaway block
			//and if not, dump the ADC reading into a particular block in the sequence
			Hispeed_Block_t& dest_block = is_throwaway ? throwaway_block : block_sequence[dest_info.block_index()];

			//then our final address is the channel index provided within the destination information
			adc_destination_addresses[i] = &dest_block.param_vals[dest_info.sub_index()];
		}

		//now we can finally fetch the next block in our sequence from memory
		current_block_index++;
		active_block = block_sequence[current_block_index];

		//we'll then check if the block for the next iteration is a valid block or not
		//can be done by checking whether any of the destination addresses are valid or not
		//implicitly exiting the loop with okay status
		//if we have the option, this would be a good time for `__builtin_expect()` for branch prediction (bias toward exiting loop)
		if(!active_block.readback_destinations[0].valid()) break; //current_block_index = 0;

		//and last thing, make sure the SPI peripherals are ready for a write for the next iteration
		//checking/stalling here rather than after CS goes low reduces latency to the SPI writes
		//checking just the first one should be sufficient, since they're all the configured identically
		//if we have the option, this would be a good time for `__builtin_expect()` for branch prediction (bias toward exiting loop)
		while(__builtin_expect(!CHANNEL_0.device_pair.READY_WRITE(), false));
	}

	//a `goto` label to skip the loop if we timed out waiting for the ALL_READY line to go high
	//most straightforward way to skip the loop
	Afterloop:

	//Diagnostic information
	//If we read the cycle counter now, we should get the number of cycles we were waiting
	//in our previous loop iteration --> shows us idle time/how much overhead we have
	volatile uint32_t cycles = Tick::get_cycles();
	(void)cycles;

	//#### UPON EXIT ####
	//signal node no longer ready
	multicard_interface.node_not_ready();

	//disable the sync timer
	multicard_interface.disable_sync_timer();

	//##### EXITING CRITICAL SECTION #####
	//can allow interrupts again
	__enable_irq();

	//disarm the hardware
	CHANNEL_0.disarm();
	CHANNEL_1.disarm();
	CHANNEL_2.disarm();
	CHANNEL_3.disarm();

	//set the appropriate flag state variable depending on the exit state
	if(exit_status == Loop_Exit_Status_t::LOOP_EXIT_ERR_SYNC_TIMEOUT)
		status_hispeed_arm_flag_err_sync_timeout.publish(true);
	else if(exit_status == Loop_Exit_Status_t::LOOP_EXIT_ERR_POWER)
		status_hispeed_arm_flag_err_pwr.publish(true);
	else if(exit_status == Loop_Exit_Status_t::LOOP_EXIT_ERR_READY)
		status_hispeed_arm_flag_err_ready.publish(true);
	else
		status_hispeed_arm_flag_complete.publish(true);

	//no longer in the armed state
	status_hispeed_armed.publish(false);

	//and finally, acknowledge the arm/fire request
	//and reschedule our main thread update function
	command_hispeed_arm_fire_request.acknowledge_reset();
	check_state_update_task.schedule_interval_ms(BIND_CALLBACK(this, check_state_update_run_esm), Scheduler::INTERVAL_EVERY_ITERATION);
}

#pragma GCC pop_options

//============================================ TEST UTILITY =============================================

void Hispeed_Subsystem::do_load_sdram_test_sequence() {
	//check if we actually want to load an SDRAM test sequence
	//return if we don't want to
	if(!command_hispeed_sdram_load_test_sequence.read()) return;

	//compute how many blocks we can build given the DRAM size
	//remember--the last block in our sequence needs to be a "terminator" i.e. have an invalid destination
	size_t num_blocks = block_memory.size() / sizeof(Hispeed_Block_t);

	//populate a square wave pattern on the blocks for now
	//or may implement just a random DAC pattern
	for(size_t i = 0; i < num_blocks - 1; i++) {
		if(true) {
			//creating a pseudorandom value using xor-shifts
			static uint16_t r = 1;
			r ^= r << 11;
			r ^= r >> 7;
			r ^= r << 3;

			//write the particular XOR value to the memory
			block_sequence[i] = Hispeed_Block_t::mk_throwaway({r, r, r, r});
		}
		else if(true) {
			//actually write back to the SDRAM in these tests
			//dump the ADC data in relatively random spaces throughout the memory
			size_t bl0 = (i + 1*num_blocks/5) % (num_blocks - 1);
			size_t bl1 = (i + 2*num_blocks/5) % (num_blocks - 1);
			size_t bl2 = (i + 3*num_blocks/5) % (num_blocks - 1);
			size_t bl3 = (i + 4*num_blocks/5) % (num_blocks - 1);

			//make our destinations "rotate"
			ADC_Destination_t d0(bl0, 1, 0);
			ADC_Destination_t d1(bl1, 2, 0);
			ADC_Destination_t d2(bl2, 3, 0);
			ADC_Destination_t d3(bl3, 0, 0);

			//and actually populate the sequence with the particular block
			block_sequence[i] = Hispeed_Block_t::mk({0, 0, 0, 0}, {d0, d1, d2, d3});
		}
		else {
			//square wave where odd blocks have command full DAC output, even blocks are 0
			if(i & 0x01)
				block_sequence[i] = Hispeed_Block_t::mk_throwaway({60000, 60000, 60000, 60000});
			else
				block_sequence[i] = Hispeed_Block_t::mk_throwaway({0, 0, 0, 0});
		}
	}

	//and make our last block a terminator
	block_sequence[num_blocks - 1] = Hispeed_Block_t::mk_term();

	//acknowledge that we've performed the action
	command_hispeed_sdram_load_test_sequence.acknowledge_reset();
}
