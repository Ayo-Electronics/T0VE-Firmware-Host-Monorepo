/*
 * app_main.cpp
 *
 *  Created on: Sep 12, 2023
 *      Author: Ishaan
 */

#include "app_main.hpp"

#include "app_shared_memory.h"
#include "app_neural_memory.hpp"
#include "app_hal_hsem.hpp"
#include "app_state_machine_library.hpp"
#include "app_hal_tick.hpp"

//instantiate our inter-core signals
Hard_Semaphore HISPEED_ARM_FIRE_READY = 	{static_cast<Hard_Semaphore::HSem_Channel>(Sem_Mapping::SEM_ARM_FIRE_READY)};
Hard_Semaphore HISPEED_ARM_FIRE_SUCCESS = 	{static_cast<Hard_Semaphore::HSem_Channel>(Sem_Mapping::SEM_ARM_FIRE_SUCCESS)};
Hard_Semaphore HISPEED_ARM_FIRE_ERR_READY = {static_cast<Hard_Semaphore::HSem_Channel>(Sem_Mapping::SEM_ARM_FIRE_ERR_READY)};	//ready monitored in hot loop
Hard_Semaphore LOSPEED_DO_ARM_FIRE = 		{static_cast<Hard_Semaphore::HSem_Channel>(Sem_Mapping::SEM_DO_ARM_FIRE)};

//also instantiate a memory helper
Neural_Memory neural_mem;

//TODO: instantiate hispeed hardware

//neural execution control variables
size_t current_block_index = 0;
__attribute__((aligned(32))) Neural_Memory::Hispeed_Block_t throwaway_block = {0};	//place on cache boundary
__attribute__((aligned(32))) Neural_Memory::Hispeed_Block_t active_block;			//place on cache boundary
Neural_Memory::Hispeed_Block_t* block_sequence = neural_mem.block_mem().data();		//array of blocks, will point to the start address in DRAM
uint16_t* __restrict adc_destination_addresses[4];									//using c-style arrays for potentially more performance

//global booleans describing error conditions and state transition conditions
bool error_interrupted = false;		//semaphore signals that process should stop (power issue, timeout during sync, timeout during node ready wait)
bool error_ready = false;			//node ready read low during hot loop
bool all_spi_ready = false;			//set to true if all SPIs are ready for writing
bool all_nodes_ready = false;		//set to true if all nodes are ready (GPIO read)

//################### HISPEED STATE MACHINE ################
//############# IDLE STATE #############
//	ON ENTRY: 	signals that we're idle, resets all state transition flags
//	LOOP: 		---
//	ON EXIT:	---
//	TRANS:		> [if high-level on DO_ARM_FIRE] transition to ARM_WAIT_NODES

void idle_ON_ENTRY() {
	//signal that we're ready, reset our `interrupted` flag
	HISPEED_ARM_FIRE_READY.LOCK();
	error_interrupted = false;
	error_ready = false;
	all_spi_ready = false;
	all_nodes_ready = false;
}

bool trans_IDLE_to_PREPARE()	{ return LOSPEED_DO_ARM_FIRE.READ(); }	//lospeed core requests an arm-fire sequence

ESM_State IDLE_state(idle_ON_ENTRY, {}, {});

//############# PREPARE STATE #############
//	ON ENTRY:	signal that we're no longer idle; configures all peripheral timing, arms all peripherals
//				resets block pointer, sets up the destination address to throwaway, makes the first block the active block
//	LOOP:		polls SPI readiness, polls timeout semaphore, sets `error_interrupted` if set
//	ON EXIT:	---
//	TRANS:		> [if error_interrupted] go to DISARM_CLEANUP
//				> [if all SPI to be ready for writing] go to ARM_WAIT_NODES
void prepare_ON_ENTRY() {
	HISPEED_ARM_FIRE_READY.UNLOCK();	//no longer idle
	neural_mem.transfer_inputs();		//moves inputs into block memory

	//reset all the global block pointers for the hot loop
	current_block_index = 0;
	active_block = block_sequence[current_block_index];
	for(size_t i = 0; i < 4; i++) adc_destination_addresses[i] = &throwaway_block.param_vals[i];

	//TODO arm peripherals
}

void prepare_LOOP() {
	//TODO: poll spi readiness
	all_spi_ready = true;

	//poll the semaphore for timeout
	error_interrupted = !LOSPEED_DO_ARM_FIRE.READ();
}

bool trans_PREPARE_to_WAIT()	{ return all_spi_ready; }		//SPI peripherals are good to go
bool trans_PREPARE_to_CLEANUP()	{ return error_interrupted; }	//timeout waiting for the SPIs to get ready

ESM_State PREPARE_state(prepare_ON_ENTRY, prepare_LOOP, {});

//############# WAIT STATE #############
//	ON ENTRY:	signals that the node is ready
//	LOOP:		polls timeout semaphore, sets `error_interrupted` if set
//	ON EXIT:	---
//	TRANS:		> [if error_interrupted] go to DISARM_CLEANUP
//				> [if nodes_ready] go to ARM_EXECUTE

void wait_ON_ENTRY() {
	//TODO: signal node is ready
}

void wait_LOOP() {
	//TODO: poll the nodes for readiness
	all_nodes_ready = true;

	//poll the semaphore for timeout
	error_interrupted = !LOSPEED_DO_ARM_FIRE.READ();
}

bool trans_WAIT_to_EXECUTE()	{ return all_nodes_ready; }		//all nodes are ready--let's go!
bool trans_WAIT_to_CLEANUP()	{ return error_interrupted; }	//timeout waiting for nodes to be ready

ESM_State WAIT_state(wait_ON_ENTRY, wait_LOOP, {});

//############# EXECUTE STATE #############
//	ON ENTRY:	> *** HOT LOOP (reset the sync_in trigger, enables sync_out timer, and everything else) ***
//	LOOP:		---
//	ON EXIT		disables sync_out timer
//	TRANS:		> [UNCONDITIONAL] transition to DISARM_CLEANUP
void HOT_LOOP();			//TODO: DECORATE WITH OPTIMIZATION, ITCM FUNCTION
void execute_ON_ENTRY() {
	HOT_LOOP();
}

void execute_ON_EXIT() {
	//TODO: disable SYNCOUT timer
}

bool trans_EXECUTE_to_CLEANUP()	{ return true; }	//unconditional transition to cleanup

ESM_State EXECUTE_state(execute_ON_ENTRY, {}, execute_ON_EXIT);

//############# CLEANUP STATE #############
//	ON ENTRY:	signals node is no longer ready, disarms all peripherals, asserts the success semaphore if no errors
//				asserts the `err_ready` semaphore if ready issue detected
//	LOOP:		---
//	ON EXIT		---
//	TRANS:		> [UNCONDITIONAL] transition to RELEASE_WAIT

void cleanup_ON_ENTRY() {
	//TODO: signal node not ready
	//TODO: disarm peripherals

	if(error_ready) HISPEED_ARM_FIRE_ERR_READY.LOCK();	//signal an READY error if our flag was set
	else if(error_interrupted);							//no semaphore flags on interruption error (handled by lospeed core)
	else {
		//success, read the outputs from the network, signal successful completion
		neural_mem.transfer_outputs();
		HISPEED_ARM_FIRE_SUCCESS.LOCK();
	}
}

bool trans_CLEANUP_to_RELEASE()	{ return true; } //unconditional transition to release state

ESM_State CLEANUP_state(cleanup_ON_ENTRY, {}, {});

//############# RELEASE STATE #############
//	ON ENTRY:	---
//	LOOP:		---
//	ON EXIT:	deasserts all status semaphores
//	TRANS:		> [if DO_ARM_FIRE is low] transition to IDLE_READY

void release_ON_EXIT() {
	//only signals the hispeed core is responsible for
	HISPEED_ARM_FIRE_ERR_READY.UNLOCK();
	HISPEED_ARM_FIRE_SUCCESS.UNLOCK();
}

bool trans_RELEASE_to_IDLE()	{ return !LOSPEED_DO_ARM_FIRE.READ(); }	//wait until the arm signal is released before returning to idle

ESM_State RELEASE_state({}, {}, release_ON_EXIT);

//#########################################
//########## STATE TRANSITIONS ############
//#########################################

ESM_Transition hispeed_trans_FROM_IDLE[1] = 	{	{&PREPARE_state, trans_IDLE_to_PREPARE}		};
ESM_Transition hispeed_trans_FROM_PREPARE[2] = 	{	{&WAIT_state, trans_PREPARE_to_WAIT},
													{&CLEANUP_state, trans_PREPARE_to_CLEANUP}	};
ESM_Transition hispeed_trans_FROM_WAIT[2] = 	{	{&EXECUTE_state, trans_WAIT_to_EXECUTE},
													{&CLEANUP_state, trans_WAIT_to_CLEANUP}		};
ESM_Transition hispeed_trans_FROM_EXECUTE[1] = 	{	{&CLEANUP_state, trans_EXECUTE_to_CLEANUP}	};
ESM_Transition hispeed_trans_FROM_CLEANUP[1] = 	{	{&RELEASE_state, trans_CLEANUP_to_RELEASE}	};
ESM_Transition hispeed_trans_FROM_RELEASE[1] = 	{	{&IDLE_state, trans_RELEASE_to_IDLE}		};

//#########################################
//############## CONTAINER ################
//#########################################
Extended_State_Machine hispeed_esm(&IDLE_state);

//#### REGISTERING ESM TRANSITIONS ####

void app_init() {
	//register our state transitions
	IDLE_state.attach_state_transitions(hispeed_trans_FROM_IDLE);
	PREPARE_state.attach_state_transitions(hispeed_trans_FROM_PREPARE);
	WAIT_state.attach_state_transitions(hispeed_trans_FROM_WAIT);
	EXECUTE_state.attach_state_transitions(hispeed_trans_FROM_EXECUTE);
	CLEANUP_state.attach_state_transitions(hispeed_trans_FROM_CLEANUP);
	RELEASE_state.attach_state_transitions(hispeed_trans_FROM_RELEASE);

	//initalize all our semaphores
	HISPEED_ARM_FIRE_SUCCESS.init();
	HISPEED_ARM_FIRE_ERR_READY.init();
	HISPEED_ARM_FIRE_READY.init();
	LOSPEED_DO_ARM_FIRE.init();

	//never want this program to be interrupted except for critical system failures (i.e. HardFault, BusFault)
	//as far as I can tell, these are non-maskable interrupts, so they'll still fire even with the following command
	//__disable_irq(); TODO: uncomment after debugging done!
}

void app_loop() {
	hispeed_esm.RUN_ESM();
}

void HOT_LOOP() {
	//TODO: reset sync-in triggers
	//TODO: enable syncout timer

	while(1) {
		//TODO: hot loop

		//a little demo simulator
		static size_t exit_code = 0;
		Tick::delay_ms(5000);
		if(exit_code%4 == 1) error_ready = true;	//simulate an error every 4 iterations
		if(exit_code%4 == 2) Tick::delay_ms(20000);	//simulate a timeout every 4 iterations
		exit_code++;

		return;
	}
}
