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
	status_device_present = temp_sensor.check_presence();
	status_temp_sensor_device_id = temp_sensor.get_device_ID();

	//start our state monitoring thread--run this every iteration of the main loop
	check_state_update_task.schedule_interval_ms(BIND_CALLBACK(this, check_state_update), Scheduler::INTERVAL_EVERY_ITERATION);

	//and also start our temperature polling task
	//stage it in one-shot mode, re-stage at the end of the function call
	//lets us immediately reschedule calls if the I2C bus is busy
	stage_temp_sensor_read_task.schedule_oneshot_ms(BIND_CALLBACK(this, stage_temp_sensor_read), TEMP_SENSOR_READ_PERIOD_MS);
}

void CoB_Temp_Monitor::disable() {
	//deschedule our temperature read task
	stage_temp_sensor_read_task.deschedule();

	//deschedule our state monitoring task
	check_state_update_task.deschedule();

	//and de-init the actual sensor
	temp_sensor.deinit();

	//finally reset all the status variables
	status_cob_temperature_c = 0;
	status_device_present = false;
	status_temp_sensor_device_id = 0;
	status_temp_sensor_error = false;
}


//on read error, just set our shared error flag
void CoB_Temp_Monitor::temp_read_error() {
	status_temp_sensor_error = true;
}

//in our thread function, just check if we need to service a temperature read
void CoB_Temp_Monitor::check_state_update() {
    //if we have some new temperature data available
    if(service_temp_sensor_read_SIGNAL.available()) {
    	//decode the new temperature data and update state
    	service_temp_sensor_read();
    }
}

//##### I2C READ FUNCTIONS #######
void CoB_Temp_Monitor::stage_temp_sensor_read() {
    //stage an I2C transaction that reads the temperature from the sensor
	//defer the actual updating of the temperature state to the main thread
    bool rx_scheduled = temp_sensor.start_read_temperature(	BIND_CALLBACK(&service_temp_sensor_read_SIGNAL, signal),
    														BIND_CALLBACK(this, temp_read_error));

    //if the transmission couldn't be scheduled, i.e. the bus was occupied, retry calling this function next iteration
    if(!rx_scheduled)
    	stage_temp_sensor_read_task.schedule_oneshot_ms(BIND_CALLBACK(this, stage_temp_sensor_read), Scheduler::ONESHOT_NEXT_ITERATION);
    //otherwise stage it again in the desired period
    else
    	stage_temp_sensor_read_task.schedule_oneshot_ms(BIND_CALLBACK(this, stage_temp_sensor_read), TEMP_SENSOR_READ_PERIOD_MS);
}

//once the read is complete, actually pull the values out of the receive buffer
void CoB_Temp_Monitor::service_temp_sensor_read() {
	//just read the temperature directly into the state variable
	status_cob_temperature_c = temp_sensor.read_temperature();
}



