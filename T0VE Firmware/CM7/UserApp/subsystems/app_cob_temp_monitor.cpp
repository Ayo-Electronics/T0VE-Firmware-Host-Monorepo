/*
 * app_cob_temp_monitor.cpp
 *
 *  Created on: Aug 15, 2025
 *      Author: govis
 */

#include "app_cob_temp_monitor.hpp"

CoB_Temp_Monitor::CoB_Temp_Monitor(Aux_I2C& _bus):
	//configuration for the temperature monitor defined in header
    temp_sensor(_bus, CoB_Temp_Monitor::SENSOR_CONFIG),

	//initialize the basic state machine
	//`enabled` is kinda an empty hook
	//`enable` and `disable` are just exit and entry functions of the disabled state
	temp_state_ENABLED({}, {}, {}),
	temp_state_DISABLED(BIND_CALLBACK(this, disable), {}, BIND_CALLBACK(this, enable)),
	esm(&temp_state_DISABLED)	//enter into the disabled state
{
	//attach the transitions between the states here
	temp_state_DISABLED.attach_state_transitions(trans_from_DISABLED);
	temp_state_ENABLED.attach_state_transitions(trans_from_ENABLED);
}

void CoB_Temp_Monitor::init() {
	//just run our extended state machine here
	//binding the `RUN_ESM` function directly for a little less overhead
	esm_exec_task.schedule_interval_ms(BIND_CALLBACK(&esm, Extended_State_Machine::RUN_ESM), Scheduler::INTERVAL_EVERY_ITERATION);
}

//=================================== PRIVATE INSTANCE METHODS =========================================
void CoB_Temp_Monitor::enable() {
	//init the temperature monitor and save the check presence status + device ID
	temp_sensor.init();
	status_device_present.publish(temp_sensor.check_presence());
	status_temp_sensor_device_id.publish(temp_sensor.get_device_ID());

	//start our state monitoring thread--run this every iteration of the main loop
	check_state_update_task.schedule_interval_ms(BIND_CALLBACK(this, check_state_update), Scheduler::INTERVAL_EVERY_ITERATION);

	//and also start our temperature polling task
	//stage it in interval mode for periodic sampling that's true to our period
	stage_temp_sensor_read_task.schedule_interval_ms(BIND_CALLBACK(&read_do, signal), TEMP_SENSOR_READ_PERIOD_MS);
}

void CoB_Temp_Monitor::disable() {
	//deschedule our temperature read task
	stage_temp_sensor_read_task.deschedule();

	//deschedule our state monitoring task
	check_state_update_task.deschedule();

	//and de-init the actual sensor
	temp_sensor.deinit();

	//finally reset all the status variables
	status_cob_temperature_c.publish(0);
	status_device_present.publish(false);
	status_temp_sensor_device_id.publish(0);
	status_temp_sensor_error.publish(false);
}

//in our thread function, just check if we need to service a temperature read
void CoB_Temp_Monitor::check_state_update() {
	//if we get a read error, update our error state
	if(read_error_listener.check()) {
		status_temp_sensor_error.publish(true);
	}

	//now check if we have data to service or if we need to kick off a read
	//make sure grabbing the data is coordinated with staging new readings in order to avoid race conditions!
	if(read_complete_listener.check()) {
		service_temp_sensor_read();
	} else if (read_do_listener.check()) {
		stage_temp_sensor_read();
	}
}

//##### I2C READ FUNCTIONS #######
void CoB_Temp_Monitor::stage_temp_sensor_read() {
    //stage an I2C transaction that reads the temperature from the sensor
	//defer the actual updating of the temperature state to the main thread
    bool rx_scheduled = temp_sensor.start_read_temperature(	&read_complete, &read_error);

    //if the transmission couldn't be scheduled, i.e. the bus was occupied, signal that we should do the read next iteration
    //otherwise, let the interval scheduler reschedule at the normal rate
    if(!rx_scheduled) read_do.signal();
}

//once the read is complete, actually pull the values out of the receive buffer
void CoB_Temp_Monitor::service_temp_sensor_read() {
	//just read the temperature directly into the state variable
	status_cob_temperature_c.publish(temp_sensor.read_temperature());
}



