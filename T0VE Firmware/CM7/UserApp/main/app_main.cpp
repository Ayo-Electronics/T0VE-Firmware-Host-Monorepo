/*
 * app_main.cpp
 *
 *  Created on: Sep 12, 2023
 *      Author: Ishaan
 */

#include "app_main.hpp"

//=========== HAL INCLUDES ===========
#include "app_hal_tick.hpp"
#include "app_hal_hsem.hpp"
#include "app_hal_pwm.hpp"
#include "app_hal_spi.hpp"
#include "app_hal_gpio.hpp"
#include "app_hal_pin_mapping.hpp"

//========== SYSTEM/UTILITY INCLUDES =========
#include "app_shared_memory.h"
#include "app_neural_memory.hpp"
#include "app_state_machine_library.hpp"

//============================== HARDWARE ============================
//#### INITIALIZE IN THIS CORE ####
//instantiate our inter-core signals
Hard_Semaphore HISPEED_ARM_FIRE_READY = 	{static_cast<Hard_Semaphore::HSem_Channel>(Sem_Mapping::SEM_ARM_FIRE_READY)};
Hard_Semaphore HISPEED_ARM_FIRE_SUCCESS = 	{static_cast<Hard_Semaphore::HSem_Channel>(Sem_Mapping::SEM_ARM_FIRE_SUCCESS)};
Hard_Semaphore HISPEED_ARM_FIRE_ERR_READY = {static_cast<Hard_Semaphore::HSem_Channel>(Sem_Mapping::SEM_ARM_FIRE_ERR_READY)};	//ready monitored in hot loop
Hard_Semaphore LOSPEED_DO_ARM_FIRE = 		{static_cast<Hard_Semaphore::HSem_Channel>(Sem_Mapping::SEM_DO_ARM_FIRE)};

//and the SPI channels used for ADC/DAC communication
//#### DON'T INITIALIZE IN THIS CORE ####
HiSpeed_SPI CHANNEL0(HiSpeed_SPI::SPI_CHANNEL_0);
HiSpeed_SPI CHANNEL1(HiSpeed_SPI::SPI_CHANNEL_1);
HiSpeed_SPI CHANNEL2(HiSpeed_SPI::SPI_CHANNEL_2);
HiSpeed_SPI CHANNEL3(HiSpeed_SPI::SPI_CHANNEL_3);

//timers used to control the chip select lines on the ADC/DAC
//#### INITIALIZE THESE IN THIS CORE ####
PWM TIM_ADC0(PWM::CS_ADC_CH0_CHANNEL);
PWM TIM_ADC1(PWM::CS_ADC_CH1_CHANNEL);
PWM TIM_ADC2(PWM::CS_ADC_CH2_CHANNEL);
PWM TIM_ADC3(PWM::CS_ADC_CH3_CHANNEL);
PWM TIM_DAC0(PWM::CS_DAC_CH0_CHANNEL);
PWM TIM_DAC1(PWM::CS_DAC_CH1_CHANNEL);
PWM TIM_DAC2(PWM::CS_DAC_CH2_CHANNEL);
PWM TIM_DAC3(PWM::CS_DAC_CH3_CHANNEL);
PWM SYNCOUT(PWM::SYNCOUT_TIMER);
PWM SYNCIN(PWM::SYNCIN_TIMER);

//a little helper wrapper
//pointers because no arrays of references
PWM* ADC_TIMERS[4] = {&TIM_ADC0, &TIM_ADC1, &TIM_ADC2, &TIM_ADC3};
PWM* DAC_TIMERS[4] = {&TIM_DAC0, &TIM_DAC1, &TIM_DAC2, &TIM_DAC3};

//GPIOs used for sync interface
//#### INITIALIZE IN THIS CORE ####
GPIO NODE_READY(Pin_Mapping::SYNC_NODE_READY);
GPIO ALL_READY(Pin_Mapping::SYNC_ALL_READY);

//function prototypes related to hardware
void INITIALIZE_HARDWARE();
void CONFIGURE_ARM_HARDWARE();
void DISARM_HARDWARE();

//########################################
//######## TIMING CONFIGURATION ##########
//########################################
//timing information for hispeed execution
//use these values to configure the timers
constexpr float CS_TIMER_PERIOD = 100e-6;	//just a large value; will automatically retrigger before this
constexpr float CS_DAC_LOWTIME = 650e-9; 	//found somewhat empirically; some delay between writing to TXDR and getting SPI out the door
constexpr float CS_ADC_LOWTIME = 1650e-9;	//maximum amount of acquisition time while still respecting conversion time
constexpr float SYNC_FREQUENCY = 500e3; 	//starting with 500kHz update frequency, will increase as timing validated
constexpr float SYNC_DUTY = 0.5;			//operating the SYNC timer at 50% duty cycle

//also instantiate a memory helper
Neural_Memory neural_mem;

//neural execution control variables
size_t current_block_index = 0;
__attribute__((aligned(32))) Neural_Memory::Hispeed_Block_t throwaway_block = {0};	//place on cache boundary
__attribute__((aligned(32))) Neural_Memory::Hispeed_Block_t active_block;			//place on cache boundary
Neural_Memory::Hispeed_Block_t* block_sequence = neural_mem.block_mem().data();		//array of blocks, will point to the start address in DRAM
uint16_t* __restrict adc_destination_addresses[4];									//using c-style arrays for potentially more performance

//global booleans describing error conditions and state transition conditions
bool error_interrupted = false;		//semaphore signals that process should stop (power issue, timeout during sync, timeout during node ready wait)
bool error_ready = false;			//node ready read low during hot loop
bool short_circuit = false;			//first memory block is invalid - skip execution entirely
bool all_spi_ready = false;			//set to true if all SPIs are ready for writing
bool all_nodes_ready = false;		//set to true if all nodes are ready (GPIO read)

//################### HISPEED STATE MACHINE ################
//############# IDLE STATE #############
//	ON ENTRY: 	signals that we're idle, resets all state transition flags
//	LOOP: 		---
//	ON EXIT:	---
//	TRANS:		> [if high-level on DO_ARM_FIRE] transition to PREPARE

void idle_ON_ENTRY() {
	//signal that we're ready, reset our `interrupted` flag
	HISPEED_ARM_FIRE_READY.LOCK();
	error_interrupted = false;
	error_ready = false;
	all_spi_ready = false;
	all_nodes_ready = false;
	short_circuit = false;
}

bool trans_IDLE_to_PREPARE()	{ return LOSPEED_DO_ARM_FIRE.READ(); }	//lospeed core requests an arm-fire sequence

ESM_State IDLE_state(idle_ON_ENTRY, {}, {});

//############# PREPARE STATE #############
//	ON ENTRY:	signal that we're no longer idle; configures all peripheral timing, arms all peripherals
//				resets block pointer, sets up the destination address to throwaway, makes the first block the active block
//	LOOP:		polls SPI readiness, polls timeout semaphore, sets `error_interrupted` if set
//	ON EXIT:	---
//	TRANS:		> [if error_interrupted] go to DISARM_CLEANUP
//				> [if all SPI to be ready for writing] go to EXECUTE
void prepare_ON_ENTRY() {
	HISPEED_ARM_FIRE_READY.UNLOCK();	//no longer idle
	neural_mem.transfer_inputs();		//moves inputs into block memory

	//reset all the global block pointers for the hot loop
	current_block_index = 0;
	active_block = block_sequence[current_block_index];
	for(size_t i = 0; i < 4; i++) adc_destination_addresses[i] = &throwaway_block.param_vals[i];

	//check if our first block is even valid--set short circuit flag if one of the values is invalid
	for(size_t i = 0; i < 4; i++) {
		if(active_block.readback_destinations[i].valid()) short_circuit = true;
	}

	//NOTE: SPI peripherals and GPIOs are armed by the lowspeed core
	//we just have to deal with timers really; take care of that here
	CONFIGURE_ARM_HARDWARE();
}

void prepare_LOOP() {
	//poll spi readiness
	all_spi_ready = CHANNEL0.ready_write() &&
					CHANNEL1.ready_write() &&
					CHANNEL2.ready_write() &&
					CHANNEL3.ready_write();

	//poll the semaphore for timeout
	error_interrupted = !LOSPEED_DO_ARM_FIRE.READ();
}

bool trans_PREPARE_to_EXECUTE()	{ return all_spi_ready; }		//SPI peripherals are good to go
bool trans_PREPARE_to_CLEANUP()	{ return error_interrupted ||	//timeout waiting for the SPIs to get ready
										 short_circuit; }		//or first block has invalid destination

ESM_State PREPARE_state(prepare_ON_ENTRY, prepare_LOOP, {});

//############# EXECUTE STATE #############
//	ON ENTRY:	> *** HOT LOOP (enables/resets the sync_in trigger, enables sync_out timer, and everything else) ***
//	LOOP:		---
//	ON EXIT		---
//	TRANS:		> [UNCONDITIONAL] transition to DISARM_CLEANUP
#pragma GCC push_options
#pragma GCC optimize ("Ofast,inline-functions")
__attribute__((section(".ITCMRAM_Section"))) //placing this function in ITCM for performance
void HOT_LOOP();
#pragma GCC pop_options

void execute_ON_ENTRY() {
	HOT_LOOP();
}

bool trans_EXECUTE_to_CLEANUP()	{ return true; }	//unconditional transition to cleanup

ESM_State EXECUTE_state(execute_ON_ENTRY, {}, {});

//############# CLEANUP STATE #############
//	ON ENTRY:	signals node is no longer ready, disarms all peripherals, asserts the success semaphore if no errors
//				asserts the `err_ready` semaphore if ready issue detected
//	LOOP:		---
//	ON EXIT		deasserts the signals once the arm command has been deasserted
//	TRANS:		> [UNCONDITIONAL] transition to RELEASE_WAIT

void cleanup_ON_ENTRY() {
	//signal node not ready
	//and disable sync input timer
	NODE_READY.set(); //active low
	SYNCOUT.disable();
	SYNCIN.disable();

	//and disarm our chip select timers
	//GPIO reconfiguration happens on the low-speed core
	DISARM_HARDWARE();

	if(error_ready) HISPEED_ARM_FIRE_ERR_READY.LOCK();	//signal an READY error if our flag was set
	else if(error_interrupted);							//no semaphore flags on interruption error (handled by lospeed core)
	else {
		//success, read the outputs from the network, signal successful completion
		neural_mem.transfer_outputs();
		HISPEED_ARM_FIRE_SUCCESS.LOCK();
	}
}

void cleanup_ON_EXIT() {
	//only signals the hispeed core is responsible for
	HISPEED_ARM_FIRE_ERR_READY.UNLOCK();
	HISPEED_ARM_FIRE_SUCCESS.UNLOCK();
}

bool trans_CLEANUP_to_IDLE()	{ return !LOSPEED_DO_ARM_FIRE.READ(); } //unconditional transition to release state

ESM_State CLEANUP_state(cleanup_ON_ENTRY, {}, cleanup_ON_EXIT);

//#########################################
//########## STATE TRANSITIONS ############
//#########################################

ESM_Transition hispeed_trans_FROM_IDLE[1] = 	{	{&PREPARE_state, trans_IDLE_to_PREPARE}		};
ESM_Transition hispeed_trans_FROM_PREPARE[2] = 	{	{&EXECUTE_state, trans_PREPARE_to_EXECUTE},
													{&CLEANUP_state, trans_PREPARE_to_CLEANUP}	};
ESM_Transition hispeed_trans_FROM_EXECUTE[1] = 	{	{&CLEANUP_state, trans_EXECUTE_to_CLEANUP}	};
ESM_Transition hispeed_trans_FROM_CLEANUP[1] = 	{	{&IDLE_state, trans_CLEANUP_to_IDLE}		};

//#########################################
//############## CONTAINER ################
//#########################################
Extended_State_Machine hispeed_esm(&IDLE_state);

//#### REGISTERING ESM TRANSITIONS ####

void app_init() {
	//register our state transitions
	IDLE_state.attach_state_transitions(hispeed_trans_FROM_IDLE);
	PREPARE_state.attach_state_transitions(hispeed_trans_FROM_PREPARE);
	EXECUTE_state.attach_state_transitions(hispeed_trans_FROM_EXECUTE);
	CLEANUP_state.attach_state_transitions(hispeed_trans_FROM_CLEANUP);

	//and initialize our hardware
	INITIALIZE_HARDWARE();

	//never want this program to be interrupted except for critical system failures (i.e. HardFault, BusFault)
	//as far as I can tell, these are non-maskable interrupts, so they'll still fire even with the following command
	__disable_irq();
}

void app_loop() {
	hispeed_esm.RUN_ESM();
}

//================= INITIALIZATION, ARMING AND CLEANUP ===================
void INITIALIZE_HARDWARE() {
	//initalize all our semaphores
	HISPEED_ARM_FIRE_SUCCESS.init();
	HISPEED_ARM_FIRE_ERR_READY.init();
	HISPEED_ARM_FIRE_READY.init();
	LOSPEED_DO_ARM_FIRE.init();

	//initialize our timers
	TIM_ADC0.init();
	TIM_ADC1.init();
	TIM_ADC2.init();
	TIM_ADC3.init();
	
	TIM_DAC0.init();
	TIM_DAC1.init();
	TIM_DAC2.init();
	TIM_DAC3.init();
	
	SYNCIN.init();
	SYNCOUT.init();
	
	//initialize our GPIO
	NODE_READY.init();
	ALL_READY.init();
	NODE_READY.set();	//default to not ready (active low)
}

void CONFIGURE_ARM_HARDWARE() {
	//configure timing information + arm all chip-select timers
	for(size_t i = 0; i < 4; i++) {
		ADC_TIMERS[i]->set_period(CS_TIMER_PERIOD);
		ADC_TIMERS[i]->set_assert_time(CS_ADC_LOWTIME);
		ADC_TIMERS[i]->reset_count(0xFFFF);	//ensures no short pulses
		ADC_TIMERS[i]->enable();

		DAC_TIMERS[i]->set_period(CS_TIMER_PERIOD);
		DAC_TIMERS[i]->set_assert_time(CS_DAC_LOWTIME);
		DAC_TIMERS[i]->reset_count(0xFFFF);	//ensures no short pulses
		DAC_TIMERS[i]->enable();
	}

	//and configure the timing for the syncout timer
	//wait until the hot-loop to enable these!
	SYNCOUT.set_frequency(SYNC_FREQUENCY);
	SYNCOUT.set_duty(SYNC_DUTY);
	SYNCOUT.reset_count(0xFFFF);	//prevents short pulses on syncout timer
}

void DISARM_HARDWARE() {
	//disable all chip select timers
	for(size_t i = 0; i < 4; i++) {
		ADC_TIMERS[i]->disable();
		DAC_TIMERS[i]->disable();
	}
}

//###########################################################################################
//####################################### HOT LOOP ##########################################
//###########################################################################################

#pragma GCC push_options
#pragma GCC optimize ("Ofast,inline-functions")

__attribute__((section(".ITCMRAM_Section"))) //PLACE THIS FUNCTION IN ITCM FOR SPEED
void HOT_LOOP() {
	NODE_READY.clear();			//signal node is ready, active low
	while(!ALL_READY.read()) {	//and wait for all nodes to be ready
		//check for timeout
		error_interrupted = !LOSPEED_DO_ARM_FIRE.READ();
		if(error_interrupted) return;
	}

	//nodes are ready at this point
	SYNCIN.reset_triggered();		//reset any sync-in timer triggers
	SYNCIN.enable();				//enable syncin timer
	SYNCOUT.enable();				//and finally enable syncout timer!

	while(1) {
		//start by waiting for DAC_CS to go low
		//	\--> can read the state of the I/O line even when in alternate mode
		//	\--> prefer prefer this, because DAC_CS follows immediately after sync RISING EDGE
		//	\--> can be edge cases with firmware-based rising edge detection that kinda scares me
		//if we have the option, this would be a good time for `__builtin_expect()` for branch prediction (bias toward exiting loop)
		while(__builtin_expect(	!SYNCIN.get_triggered() &&	//wait for sync interface timer to be triggered
								LOSPEED_DO_ARM_FIRE.READ(),	//AND make sure we don't time out (we still wanna execute)
				false));

		//ALL SPI buses should be ready for data
		//dump the parameters from the current block into the DACs via SPI bus
		CHANNEL0.write(active_block.param_vals[0]);
		CHANNEL1.write(active_block.param_vals[1]);
		CHANNEL2.write(active_block.param_vals[2]);
		CHANNEL3.write(active_block.param_vals[3]);

		//clear the triggered flag for the syncin timer
		SYNCIN.reset_triggered();

		//this is a good time to check our exit conditions
		//checking here rather than in the CS wait hot loop reduces latency to the SPI writes
		if(!ALL_READY.read()) {
			//checking whether all nodes are still good for execution
			//if they aren't leave the loop
			error_ready = true;
			return;
		} else if(!LOSPEED_DO_ARM_FIRE.READ()) {
			//checking whether we timed out via the semaphore
			error_interrupted = true;
			return;
		}

		//while we're waiting, for the transaction to complete prefetch the next block in the block sequence
		//should speed up our fetch of the next block
		__builtin_prefetch(&block_sequence[current_block_index + 1]);

		//and now actually wait for the transactions to complete
		//checking the first channel should be sufficient, since all channels are basically the same
		//if we have the option, this would be a good time for `__builtin_expect()` for branch prediction (bias toward exiting loop)
		while(__builtin_expect(!CHANNEL0.ready_read(), false));

		//all SPI buses should be ready for reading
		//dump the ADC data directly into the memory addresses we calculated from the previous iteration
		//	\--> on the first iteration, dump them into the throwaway
		*adc_destination_addresses[0] = CHANNEL0.read();
		*adc_destination_addresses[1] = CHANNEL1.read();
		*adc_destination_addresses[2] = CHANNEL2.read();
		*adc_destination_addresses[3] = CHANNEL3.read();

		//the last thing we'll do before fetching our next block
		//is to calculate the memory addresses we want to dump the results of this current execution
		//do so in this loop (asking the compilier to unroll this loop for speed)
		#pragma GCC unroll 4
		for(size_t i = 0; i < 4; i++){
			//grab information regarding the destination that we'd like to place the particular ADC channel
			//will contain a block index, a specific channel index (0-4), and whether we should just discard the ADC value
			//reference, so we don't spend extra instructions copying
			Neural_Memory::ADC_Destination_t& dest_info = active_block.readback_destinations[i];

			//check if the particular ADC value is a throwaway or not
			bool is_throwaway = dest_info.throwaway();

			//if it is, dump the ADC reading into the throwaway block
			//and if not, dump the ADC reading into a particular block in the sequence
			Neural_Memory::Hispeed_Block_t& dest_block = is_throwaway ? throwaway_block : block_sequence[dest_info.block_index()];

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
		if(!active_block.readback_destinations[0].valid()) return; //current_block_index = 0;

		//and last thing, make sure the SPI peripherals are ready for a write for the next iteration
		//checking/stalling here rather than after CS goes low reduces latency to the SPI writes
		//checking just the first one should be sufficient, since they're all the configured identically
		//if we have the option, this would be a good time for `__builtin_expect()` for branch prediction (bias toward exiting loop)
		while(__builtin_expect(!CHANNEL0.ready_write(), false));
	}
}
#pragma GCC pop_options

//use to diagnose lowspeed core
void DEMO_HOT_LOOP() {
	//need tick interrupts for this to work
	__enable_irq();

	//a little demo simulator
	static size_t exit_code = 0;
	Tick::delay_ms(5000);
	if(exit_code%4 == 1) error_ready = true;	//simulate an error every 4 iterations
	if(exit_code%4 == 2) Tick::delay_ms(20000);	//simulate a timeout every 4 iterations
	exit_code++;

	//disable tick interrupts before we leave
	__disable_irq();

	return;
}
