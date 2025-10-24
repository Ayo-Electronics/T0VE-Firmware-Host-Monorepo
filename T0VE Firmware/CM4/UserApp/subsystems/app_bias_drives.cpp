/*
 * app_bias_drives.cpp
 *
 *  Created on: Oct 1, 2025
 *      Author: govis
 */

#include "app_bias_drives.hpp"

Waveguide_Bias_Drive::Waveguide_Bias_Drive(	Aux_I2C& _bus,
											const GPIO::GPIO_Hardware_Pin& _reg_enable_pin,
											const GPIO::GPIO_Hardware_Pin& _dac_reset_pin):
	//configuration for the bias DAC,
	bias_dac_0x0C(_bus, AD5675::I2C_Address::AD5675_0x0C),
	bias_dac_0x0F(_bus, AD5675::I2C_Address::AD5675_0x0F),

	//initialize the TX state machine
	bias_tx_state_IDLE({}, {}, BIND_CALLBACK(this, tx_IDLE_on_exit)),
	bias_tx_state_TX({}, BIND_CALLBACK(this, tx_TX_thread_func), {}),
	bias_tx_state_INCREMENT(BIND_CALLBACK(this, tx_INCREMENT_on_entry), {}, {}),
	esm_tx(&bias_tx_state_IDLE), //enter into the IDLE state

	//initialize the RX state machine
	bias_rx_state_IDLE({}, {}, BIND_CALLBACK(this, rx_IDLE_on_exit)),
	bias_rx_state_REQUEST1({}, BIND_CALLBACK(this, rx_REQUEST1_thread_func), {}),
	bias_rx_state_WAIT1({}, {}, {}),
	bias_rx_state_REQUEST2({}, BIND_CALLBACK(this, rx_REQUEST2_thread_func), {}),
	bias_rx_state_WAIT2({}, {}, {}),
	bias_rx_state_RBUPDATE(BIND_CALLBACK(this, rx_RBUPDATE_on_entry), {}, {}),
	esm_rx(&bias_rx_state_IDLE), //enter into the IDLE state

	//initialize the basic state machine
	//enabled state is empty, `enable` and `disable` functions are just exit and entry functions of the `disabled` state
	bias_state_ENABLED({}, {}, {}),
	bias_state_DISABLED(BIND_CALLBACK(this, disable), {}, BIND_CALLBACK(this, enable)),
	esm_supervisor(&bias_state_DISABLED), //enter into the disabled state

	//and finally, initialize things relevant to the GPIO control
	dac_reset_line(_dac_reset_pin),
	reg_enable(_reg_enable_pin)

{
	//attach the transitions between the states here - enable/disable ESM
	bias_state_DISABLED.attach_state_transitions(trans_from_DISABLED);
	bias_state_ENABLED.attach_state_transitions(trans_from_ENABLED);

	//state transitions for tx state machine
	bias_tx_state_IDLE.attach_state_transitions(tx_trans_from_IDLE);
	bias_tx_state_TX.attach_state_transitions(tx_trans_from_TX);
	bias_tx_state_INCREMENT.attach_state_transitions(tx_trans_from_INCREMENT);

	//state transitions for rx state machine
	bias_rx_state_IDLE.attach_state_transitions(rx_trans_from_IDLE);
	bias_rx_state_REQUEST1.attach_state_transitions(rx_trans_from_REQUEST1);
	bias_rx_state_REQUEST2.attach_state_transitions(rx_trans_from_REQUEST2);
	bias_rx_state_WAIT1.attach_state_transitions(rx_trans_from_WAIT1);
	bias_rx_state_WAIT2.attach_state_transitions(rx_trans_from_WAIT2);
	bias_rx_state_RBUPDATE.attach_state_transitions(rx_trans_from_RBUPDATE);
}

void Waveguide_Bias_Drive::init() {
	//initialize our GPIOs
	dac_reset_line.init();
	dac_reset_line.clear();
	reg_enable.init();
	reg_enable.clear();

	//just run our state machine here --> directly bind the RUN_ESM function
	//automatically calls enable/disable when appropriate
	esm_supervisor_task.schedule_interval_ms(	BIND_CALLBACK(&esm_supervisor, Extended_State_Machine::RUN_ESM),
												Scheduler::INTERVAL_EVERY_ITERATION);
}

//=================================== PRIVATE INSTANCE METHODS =========================================
void Waveguide_Bias_Drive::enable() {
	//bring the DAC reset line high
	dac_reset_line.set();
	Tick::delay_ms(1); //let the reset line settle before doing any other DAC writes, adjust as necessary

	//init both DACs
	bias_dac_0x0C.init();
	bias_dac_0x0F.init();

	//and check if both are on the bus
	//only report devices present if both are present
	bool dacs_present = bias_dac_0x0C.check_presence();
	dacs_present = dacs_present && bias_dac_0x0F.check_presence();
	status_device_present.publish(dacs_present);

	//update the DACs with our programmed state
	write_do.signal();

	//reset our regulator enable state
	//and update our regulator enable pin accordingly (should already be disabled, but just in case)
	command_bias_reg_enable.acknowledge_reset();
	do_regulator_ctrl_update();

	//stage our esm tx/rx tasks, our periodic read "kickoff" task, and our regulator GPIO control task
	check_state_update_task.schedule_interval_ms(BIND_CALLBACK(this, check_state_update), Scheduler::INTERVAL_EVERY_ITERATION);
	esm_tx_rx_task.schedule_interval_ms(BIND_CALLBACK(this, run_tx_rx_esm), Scheduler::INTERVAL_EVERY_ITERATION);
	periodic_read_task.schedule_interval_ms(BIND_CALLBACK(&read_do, signal), READ_BIAS_DAC_VALUES_PERIOD_MS);
}

void Waveguide_Bias_Drive::disable() {
	//deschedule the tx/rx state machines, periodic read task, and GPIO control thread
	esm_tx_rx_task.deschedule();
	periodic_read_task.deschedule();
	check_state_update_task.deschedule();

	//reset the tx/rx state machines so they go back into IDLE
	esm_tx.RESET_ESM();
	esm_rx.RESET_ESM();

	//deinit the DACs, reset the DACs
	bias_dac_0x0C.deinit();
	bias_dac_0x0F.deinit();
	dac_reset_line.clear();

	//bring regulator enable line low
	reg_enable.clear();

	//and finally, reset all the status state variables
	status_device_present.publish(false);
	status_bias_dac_error.publish(false);
	status_bias_dac_values_readback.publish({0});
	command_bias_reg_enable.acknowledge_reset();
	//retaining the bias DAC values as a convenience "persistent storage" between power cycles
	//uncomment below to reset bias DAC values between power cycles
	//command_bias_dac_values.acknowledge_reset();
}

//=================== GENERAL STATE UPDATE =====================
void Waveguide_Bias_Drive::check_state_update() {
	//if we have a new regulator enable command value
	if(command_bias_reg_enable.check()) {
		do_regulator_ctrl_update();
	}

	//if we had any read or write errors, publish them accordingly
	if(read_error_listener_pubstate.check()) {
		//set the upstream state variable and acknowledge the read command
		status_bias_dac_error.publish(true);
		command_bias_dac_read_update.acknowledge_reset();
	}
	if(write_error_listener_pubstate.check()) {
		//just set the upstream state variable
		status_bias_dac_error.publish(true);
	}
}

//=================== TX STATE MACHINE IMPLEMENTATION ================
//called when we've commanded a write, exiting idle
void Waveguide_Bias_Drive::tx_IDLE_on_exit() {
	//update the channel-wise bias setpoints
	Bias_Setpoints_t::update_collection(bias_setpoints, bulk_mapping, mid_mapping, stub_mapping, command_bias_dac_values.read());

	//reset our setpoint write index
	bias_setpoint_tx_index = 0;

	//clear any write errors
	write_error_listener.refresh();
}

//called while in the transmitting state
void Waveguide_Bias_Drive::tx_TX_thread_func() {
	//pull the destination information from our setpoint array
	Bias_Setpoints_t& sp = bias_setpoints[bias_setpoint_tx_index];

	//try to write to the particular channel
	//if the write scheduling is successful, we'll automatically switch states to INCREMENT
	tx_transfer_staged = sp.mapping.dac->write_channel(	sp.mapping.channel,
														sp.channel_val,
														&write_error);

}

void Waveguide_Bias_Drive::tx_INCREMENT_on_entry() {
	//just increment our setpoint index
	//will automatically return to idle if our setpoint index indicates that we've sent all data
	bias_setpoint_tx_index++;
}

//=================== RX STATE MACHINE IMPLEMENTATION ================
//reset all thread signals coming out of the IDLE state
void Waveguide_Bias_Drive::rx_IDLE_on_exit() {
	read_do_listener.refresh();
	read_complete_listener.refresh();
	read_error_listener.refresh();
}

//stage a transmission to the first DAC
void Waveguide_Bias_Drive::rx_REQUEST1_thread_func() {
	//try to read from the particular DAC
	//if the read was successfully staged, automatically move to the next (wait) state
	//set the thread signal directly in the read complete callback function
	rx_transfer_success = bias_dac_0x0C.start_dac_readback(&read_complete, &read_error);
}

//stage a transmission to the second DAC
void Waveguide_Bias_Drive::rx_REQUEST2_thread_func() {
	//try to read from the particular DAC
	//if the read was successfully staged, automatically move to the next (wait) state
	//set the thread signal directly in the read complete callback function
	rx_transfer_success = bias_dac_0x0F.start_dac_readback(&read_complete, &read_error);
}

//parse responses from DACs, update state variables, acknowledge read
void Waveguide_Bias_Drive::rx_RBUPDATE_on_entry() {
	//state variable temporary
	Waveguide_Bias_Setpoints_t _bias_readback = {0};

	//pull the data from the DACs
	auto dac_readback_0x0C = bias_dac_0x0C.dac_readback();
	auto dac_readback_0x0F = bias_dac_0x0F.dac_readback();

	//quick lamba function to help data placement based on mapping
	auto place_readback = [&](	std::span<uint16_t, std::dynamic_extent> readback_container,
								std::span<const Waveguide_Bias_Mapping_t, std::dynamic_extent> mapping)
		{
		//go through all items in the arrays
		for(size_t i = 0; i < mapping.size(); i++) {
			//pick which dac we're pulling data from based off mapping
			const auto& dac_readback_array = (mapping[i].dac == &bias_dac_0x0F) ? dac_readback_0x0F : dac_readback_0x0C;

			//place the data from the appropriate channel in the readback container based on mapping
			readback_container[i] = dac_readback_array[mapping[i].channel];
		}

	};

	//place data based on lambda
	place_readback(_bias_readback.bulk_setpoints, bulk_mapping);
	place_readback(_bias_readback.mid_setpoints, mid_mapping);
	place_readback(_bias_readback.stub_setpoints, stub_mapping);

	//update state variable, acknowledge that the read has completed
	status_bias_dac_values_readback.publish(_bias_readback);
	command_bias_dac_read_update.acknowledge_reset();
}

//=================== REGULATOR ENABLE CONTROL ==================
//function that updates the regulator enable pin
//pretty straightforward, just check boolean
void Waveguide_Bias_Drive::do_regulator_ctrl_update() {
	if(command_bias_reg_enable.read()) reg_enable.set();
	else reg_enable.clear();
}
