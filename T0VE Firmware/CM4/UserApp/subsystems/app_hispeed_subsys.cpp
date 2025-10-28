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
										MSC_Interface& _msc_if):
	//construct DRAM, save the MSC interface and construct the memory helper
	dram(DRAM::DRAM_INTERFACE),
	msc_if(_msc_if),
	mem_helper(&dram, &msc_if),

	//initialize each channel with the hardware provided
	CHANNEL_0(ch0), CHANNEL_1(ch1),	CHANNEL_2(ch2),	CHANNEL_3(ch3),

	//then construct the state machine states
	hispeed_state_INACTIVE(BIND_CALLBACK(this, deactivate), {}, {}),
	hispeed_state_ACTIVE(BIND_CALLBACK(this, activate), {}, {}),
	hispeed_state_PREARM(BIND_CALLBACK(this, do_prearm_check), {}, {}),
	hispeed_state_ARM_FIRE(	BIND_CALLBACK(this, do_arm_fire_setup),
							BIND_CALLBACK(this, do_arm_fire_run),
							BIND_CALLBACK(this, do_arm_fire_exit)	),

	//and finally the state machine itself
	//enter into the inactive state
	hispeed_esm(&hispeed_state_INACTIVE)
{
	//link the state machine transitions here
	hispeed_state_INACTIVE.attach_state_transitions(hispeed_trans_FROM_INACTIVE);
	hispeed_state_ACTIVE.attach_state_transitions(hispeed_trans_FROM_ACTIVE);
	hispeed_state_ARM_FIRE.attach_state_transitions(hispeed_trans_FROM_ARM_FIRE);
}

//===================== INITIALIZATION FUNCTIONS ====================
void Hispeed_Subsystem::init() {
	//initialize the memory helper
	mem_helper.init();

	//initialize all of our high-speed channels with the helper functions
	CHANNEL_0.init();
	CHANNEL_1.init();
	CHANNEL_2.init();
	CHANNEL_3.init();

	//initialize all our semaphores
	LOSPEED_DO_ARM_FIRE.init();
	LOSPEED_IMMEDIATE_PGOOD.init();
	HISPEED_ARM_FIRE_ERR_PWR.init();
	HISPEED_ARM_FIRE_ERR_READY.init();
	HISPEED_ARM_FIRE_ERR_SYNC.init();
	HISPEED_ARM_FIRE_READY.init();
	HISPEED_ARM_FIRE_SUCCESS.init();

	//TODO: figure out how to push the timing configurations--will likely do this in the high-speed side

	//configure the chip select timing for all the channels
//	CHANNEL_0.configure_timing(CS_DAC_LOWTIME, CS_ADC_LOWTIME);
//	CHANNEL_1.configure_timing(CS_DAC_LOWTIME, CS_ADC_LOWTIME);
//	CHANNEL_2.configure_timing(CS_DAC_LOWTIME, CS_ADC_LOWTIME);
//	CHANNEL_3.configure_timing(CS_DAC_LOWTIME, CS_ADC_LOWTIME);

	//and configure the SYNCOUT timer with our desired frequency and duty cycle
//	multicard_interface.configure_sync_timer(SYNC_FREQUENCY, SYNC_DUTY);

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
	hispeed_pilot_task.schedule_interval_ms(BIND_CALLBACK(&pilot_signal, signal), PILOT_TASK_PERIOD_MS);
}

//PRIVATE function
void Hispeed_Subsystem::deactivate() {
	//deschedule the pilot task
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

//===================== THREAD FUNCTIONS ======================
void Hispeed_Subsystem::check_state_update_run_esm() {
	//set the semaphore status based on the PGOOD status
	if(status_onboard_immediate_pgood.read()) {
		LOSPEED_IMMEDIATE_PGOOD.TRY_LOCK();	//should always work, but non-blocking in case
	}
	else LOSPEED_IMMEDIATE_PGOOD.UNLOCK();

	//and if we're signaled, run the pilot
	if(pilot_signal_listener.check()) {
		do_hispeed_pilot();
	}

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
//pre-arm check makes sure our second core is ready to execute
//potentially timeout waiting for ready
void Hispeed_Subsystem::do_prearm_check() {
	//all this does is clear pending timeouts and reschedule the timeout
	//actual checking if the peripheral is ready is done during state transition check
	arm_fire_timeout_task.schedule_oneshot_ms(BIND_CALLBACK(this, do_prearm_fail), PREARM_TIMEOUT_MS);
	arm_fire_timeout_listener.refresh();	//clear pending timeouts
}

void Hispeed_Subsystem::do_prearm_fail() {
	//called on timeout, signal the timeout listener
	arm_fire_timeout.signal();

	//also acknowledge the ARM flag, and propagate a timeout error upstream
	command_hispeed_arm_fire_request.acknowledge_reset();
	status_hispeed_arm_flag_err_core_timeout.publish(true);
}

//the hispeed network execution function now runs on the other core (CM7)
//this is such that we can run all normal keepalive functions concurrently with network execution (which may take some time)
//this keeps USB comms alive, meaning faster I/O (since we don't have to re-enumerate on completion)
void Hispeed_Subsystem::do_arm_fire_setup() {
	//start by signalling that we're in the armed state
	status_hispeed_armed.publish(true);

	//deschedules the pilot task, clears the signal
	//NOTE: this task automatically gets restarted when we go back into `ACTIVE`
	hispeed_pilot_task.deschedule();
	pilot_signal_listener.refresh();

	//prevent the files from being accessed over USB
	//and move the inputs into the block memory
	mem_helper.detach_files();
	mem_helper.transfer_inputs();

	//arm the channels
	//i.e. puts the chip select lines under timer control
	CHANNEL_0.arm();
	CHANNEL_1.arm();
	CHANNEL_2.arm();
	CHANNEL_3.arm();

	//schedule our timeout
	arm_fire_timeout_task.schedule_oneshot_ms(BIND_CALLBACK(&arm_fire_timeout, signal), FIRING_TIMEOUT_MS);

	//clear any pending "done" and "timeout" signals
	arm_fire_done_listener.refresh();
	arm_fire_timeout_listener.refresh();

	//and signal the second core to run!
	//potentially blocking lock to ensure state propagates
	LOSPEED_DO_ARM_FIRE.LOCK();
}

void Hispeed_Subsystem::do_arm_fire_run() {
	//while we're running, just poll the completion flags
	//the scheduler will trigger the timeout independently of this function
	if		(HISPEED_ARM_FIRE_SUCCESS.READ()) 	arm_fire_done.signal();
	else if	(HISPEED_ARM_FIRE_ERR_PWR.READ()) 	arm_fire_done.signal();
	else if (HISPEED_ARM_FIRE_ERR_READY.READ())	arm_fire_done.signal();
	else if (HISPEED_ARM_FIRE_ERR_SYNC.READ())	arm_fire_done.signal();
}

void Hispeed_Subsystem::do_arm_fire_exit() {
	//deschedule our timeout checker
	arm_fire_timeout_task.deschedule();

	//disarm the hardware immediately after finish
	CHANNEL_0.disarm();
	CHANNEL_1.disarm();
	CHANNEL_2.disarm();
	CHANNEL_3.disarm();

	//grabs the exit codes, propagate exit signals to state variables
	//deasserts fire signal, unlocks memory, moves the outputs, attaches the files, restores peripherals
	bool success = false;
	if		(HISPEED_ARM_FIRE_ERR_PWR.READ()) 	status_hispeed_arm_flag_err_pwr.publish(true);
	else if (HISPEED_ARM_FIRE_ERR_READY.READ())	status_hispeed_arm_flag_err_ready.publish(true);
	else if (HISPEED_ARM_FIRE_ERR_SYNC.READ())	status_hispeed_arm_flag_err_sync.publish(true);
	else if (HISPEED_ARM_FIRE_SUCCESS.READ()) {
		success = true;
		status_hispeed_arm_flag_complete.publish(true);
	}
	else /* exit without flags means timeout */	status_hispeed_arm_flag_err_core_timeout.publish(true);

	//now that we've grabbed the exit codes, we can deassert our fire signal
	LOSPEED_DO_ARM_FIRE.UNLOCK();

	//if we got a valid network output, fetch outputs from the block memory
	//and expose all the files for editing again
	if(success) mem_helper.transfer_outputs();
	mem_helper.attach_files();

	//finally acknowledge the fire request command and indicate that we're no longer armed
	status_hispeed_armed.publish(false);
	command_hispeed_arm_fire_request.acknowledge_reset();
}
