/*
 * app_power_monitor.cpp
 *
 *  Created on: Jun 25, 2025
 *      Author: govis
 */

#include "app_hal_tick.hpp"
#include "app_power_monitor.hpp"

Power_Monitor::Power_Monitor(	const GPIO::GPIO_Hardware_Pin _pwr_reg_en, const GPIO::GPIO_Hardware_Pin _pwr_pgood,
								uint32_t _debounce_time_ms, bool _ENABLE_POL_INVERTED, bool _PGOOD_POL_INVERTED):
	//save the IO polarity
	ENABLE_POL_INVERTED(_ENABLE_POL_INVERTED),
	PGOOD_POL_INVERTED(_PGOOD_POL_INVERTED),

	//save the debounce time
	debounce_time_ms(_debounce_time_ms),

	//initialize the GPIO pin controls from the hardware
    pwr_reg_en(_pwr_reg_en),
    pwr_pgood(_pwr_pgood)

    //scheduler and callback functions are default initialized
{}

void Power_Monitor::init() {
    //initialize the GPIO pins
    pwr_reg_en.init();
    pwr_pgood.init();

    //delay for a little bit so the GPIO pins can settle
    Tick::delay_ms(debounce_time_ms);

    //initialize the power good status to the value of the PGOOD pin
    debounce_average = get_immediate_power_status() ? 1 : 0;

    //enable the regulators to the present value of the state
    do_en_dis_reg();

    //start the scheduler task
    check_power_status_task.schedule_interval_ms(BIND_CALLBACK(this, check_power_status), debounce_time_ms);
    check_state_update_task.schedule_interval_ms(BIND_CALLBACK(this, check_state_update), 10); //relax interval to 10ms
}

//=================================== INTERNAL FUNCTIONS ===================================

void Power_Monitor::check_power_status() {
    //debouncing performed with exponential moving average (i.e. single-pole IIR low pass filter, equivalent to RC filter)
    //I know this is kinda computation heavy for something so simple, but it's fairly straightforward to understand
    //logic is also pretty easy to debug
    float current_power_status = get_immediate_power_status() ? 1.0f : 0.0f;

    debounce_average = debounce_average * MOVING_AVERAGE_DECAY_FACTOR + (1.0f - MOVING_AVERAGE_DECAY_FACTOR) * current_power_status;

    //check if we've crossed hysteresis thresholds
    if(debounce_average > THRESHOLD_HIGH) local_debounced_power_status = true;
    else if(debounce_average < THRESHOLD_LOW) local_debounced_power_status = false;
}

void Power_Monitor::check_state_update() {
    //check to see if we want to change the power monitor state
	if(command_regulator_enabled.check())
		//defer the state update actualization in a different function call
		do_en_dis_reg();

	//and update our state variables accoding to our locals
	status_immedate_power.publish(get_immediate_power_status());
	status_debounced_power.publish(local_debounced_power_status);
}

//=================================== PRIVATE READ/WRITE FUNCTIONS ===================================

void Power_Monitor::do_en_dis_reg() {
	//state change in the power regulators
	if(command_regulator_enabled.read()) enable_reg(); //if we wanted to enable the regulators, enable them
	else disable_reg(); //otherwise disable them
}

void Power_Monitor::enable_reg() {
    //set the enable pin to the appropriate value
    if(ENABLE_POL_INVERTED) pwr_reg_en.clear();
    else pwr_reg_en.set();
}

void Power_Monitor::disable_reg() {
    //set the enable pin to the appropriate value
    if(ENABLE_POL_INVERTED) pwr_reg_en.set();
    else pwr_reg_en.clear();
}

