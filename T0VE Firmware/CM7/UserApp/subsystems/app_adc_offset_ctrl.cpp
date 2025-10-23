/*
 * app_adc_offset_ctrl.cpp
 *
 *  Created on: Aug 15, 2025
 *      Author: govis
 */

#include "app_adc_offset_ctrl.hpp"

ADC_Offset_Control::ADC_Offset_Control(Aux_I2C& _bus):
	//configuration for the offset DAC
    offset_dac(_bus, 	MCP4728::MCP4728_ADDRESS_0x60,
    					MCP4728::MCP4728_VREF_INT2p048,
						MCP4728::MCP4728_GAIN_1x,
						MCP4728::MCP4728_LDAC_LOW),

	//initialize the basic state machine
	//enabled state is empty, `enable` and `disable` functions are just exit and entry functions of the `disabled` state
	offset_state_ENABLED({}, {}, {}),
	offset_state_DISABLED(BIND_CALLBACK(this, disable), {}, BIND_CALLBACK(this, enable)),
	esm(&offset_state_DISABLED)	//enter into the disabled state
{
	//attach the transitions between the states here
	offset_state_DISABLED.attach_state_transitions(trans_from_DISABLED);
	offset_state_ENABLED.attach_state_transitions(trans_from_ENABLED);
}

void ADC_Offset_Control::init() {
	//just run our state machine here --> directly bind the RUN_ESM function
	//automatically calls enable/disable when appropriate
	esm_exec_task.schedule_interval_ms(BIND_CALLBACK(&esm, Extended_State_Machine::RUN_ESM), Scheduler::INTERVAL_EVERY_ITERATION);
}

//=================================== PRIVATE INSTANCE METHODS =========================================
void ADC_Offset_Control::enable() {
	//init the DAC and check if it's on the bus
	offset_dac.init();
	status_device_present.publish(offset_dac.check_presence());

	//update our DAC with its default configuration of values
	//and readback the the values we wrote to the DAC if we want
	do_write_offset_dac_values();
	do_read_offset_dac_values();

	//start our state monitoring thread--run this every iteration of the main loop
	check_state_update_task.schedule_interval_ms(BIND_CALLBACK(this, check_state_update), Scheduler::INTERVAL_EVERY_ITERATION);

	//and one-shot schedule our DAC readback task
	//will get rescheduled every iteration--one-shot scheduling lets schedule in the next loop iteration if I2C was busy
	dac_periodic_read.schedule_interval_ms(BIND_CALLBACK(&do_read_signal, signal), READ_OFFSET_DAC_VALUES_PERIOD_MS);
}

void ADC_Offset_Control::disable() {
	//deschedule the periodic read task
	dac_periodic_read.deschedule();

	//deschedule our state monitoring thread
	check_state_update_task.deschedule();

	//deinit the DAC
	offset_dac.deinit();

	//and finally, reset all the status state variables
	status_device_present.publish(false);
	status_offset_dac_error.publish(false);
	status_offset_dac_values_readback.publish({0, 0, 0, 0});
}

void ADC_Offset_Control::check_state_update() {
	//##### CHECK ERROR FLAGS #####
	//if our error flags are set, update our state variables
	if(read_error_listener.check()) {
		status_offset_dac_error.publish(true);
		command_offset_dac_read_update.acknowledge_reset();
	}
	if(write_error_listener.check()) {
		status_offset_dac_error.publish(true);
	}

	//#### WRITE DAC VALUES #####
    //go through each command state variable and take actions accordingly if they're set
    if(command_offset_dac_values.check()) do_write_signal.signal();	//signal that we want to do a write
    if(do_write_listener.check()) {
    	//we were signaled to perform a write either by the command
    	//or by a deferred write after the I2C bus was busy
    	do_write_offset_dac_values();
    }

    //#### DAC READBACK ####
    //if we get a read request command, assert our signal
    if(command_offset_dac_read_update.check()) do_read_signal.signal();

    //coordinate staging new reads and extracting data from completed reads
    //make sure that they can never happen at the same time due to race conditions!
	if(read_complete_listener.check()) {	//if we have data to be read
		service_offset_dac_read();
	} else if(do_read_listener.check()) {	//if we don't have data to be read and we wanna do another read
        do_read_offset_dac_values();
    }
}

//##### METHODS TO DEFER I2C WRITES #######
void ADC_Offset_Control::do_write_offset_dac_values() {
    //write the DAC values to the output register
    bool tx_scheduled = offset_dac.write_channels(command_offset_dac_values.read(), &write_error_flag);

    //if the transmission couldn't be scheduled, i.e. the bus was occupied,
    //signal that we'd like to retry calling this function next iteration
    if(!tx_scheduled) do_write_signal.signal();
}

void ADC_Offset_Control::do_read_offset_dac_values() {
    //read the DAC values from the output register
	//defer the actual reading of the DAC values to the main system update listener thread
    bool rx_scheduled = offset_dac.start_read_update_status(&read_complete_flag, &read_error_flag);

    //if the transmission couldn't be scheduled, i.e. the bus was occupied,
    //indicate that we'd like to retry calling this function next iteration
    if(!rx_scheduled) do_read_signal.signal();
}

//once the read is complete, actually pull the values out of the receive buffer
void ADC_Offset_Control::service_offset_dac_read() {
	//get the status information into a temporary variable
	auto dac_status = offset_dac.read_update_status();

	//acknowledge that we have new data--signal to any upstream threads by clearing this flag
	command_offset_dac_read_update.acknowledge_reset();

	//and for now, just beam up the DAC values actively being commanded
	status_offset_dac_values_readback.publish(dac_status.dac_vals);
}

