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
	//intialize our state variables to suitable defaults
	immediate_power_status(false),
	debounced_power_status(false),
	command_regulator_enabled(),

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
    //and initialize the debounce average appropriately in order to not trigger a callback immediately
    immediate_power_status = get_immediate_power_status();
    debounced_power_status = (bool)immediate_power_status;
    debounce_average = debounced_power_status ? 1 : 0;

    //enable the regulators to the present value of the state
    do_en_dis_reg();

    //start the scheduler task
    check_power_status_task.schedule_interval_ms(BIND_CALLBACK(this, check_power_status), debounce_time_ms);
    check_state_update_task.schedule_interval_ms(BIND_CALLBACK(this, check_state_update), Scheduler::INTERVAL_EVERY_ITERATION);
}

//=================================== CALLBACK FUNCTIONS ===================================

void Power_Monitor::register_power_became_good(Callback_Function<> callback, bool _enable_callbacks) {
    power_became_good = callback;
    if(_enable_callbacks) enable_callbacks();
}

void Power_Monitor::register_power_became_bad(Callback_Function<> callback, bool _enable_callbacks) {
    power_became_bad = callback;
    if(_enable_callbacks) enable_callbacks();
}

void Power_Monitor::enable_callbacks() {
	callbacks_enabled = true;
}

void Power_Monitor::disable_callbacks() {
	callbacks_enabled = false;
}

//=================================== INTERNAL FUNCTIONS ===================================

void Power_Monitor::check_power_status() {
    //debouncing performed with exponential moving average
    //I know this is kinda computation heavy for something so simple, but it's fairly straightforward to understand
    //logic is also pretty easy to debug
    bool _immediate_power_status = get_immediate_power_status();
    immediate_power_status = _immediate_power_status; //update our shared system state variable
    float current_power_status = _immediate_power_status ? 1.0f : 0.0f;

    debounce_average = debounce_average * MOVING_AVERAGE_DECAY_FACTOR + (1.0f - MOVING_AVERAGE_DECAY_FACTOR) * current_power_status;

    //check if we've crossed hysteresis thresholds
    //fire off callbacks if enabled
    if(debounce_average > THRESHOLD_HIGH && debounced_power_status == false) {
        debounced_power_status = true; //update our shared system state variable
        if(callbacks_enabled) power_became_good();
    }
    else if(debounce_average < THRESHOLD_LOW && debounced_power_status == true) {
        debounced_power_status = false;
        if(callbacks_enabled) power_became_bad();
    }
}

void Power_Monitor::check_state_update() {
    //check to see if we want to change the power monitor state
	if(command_regulator_enabled.available())
		//defer the state update actualization in a different function call
		do_en_dis_reg();
}

//=================================== PRIVATE READ/WRITE FUNCTIONS ===================================

void Power_Monitor::do_en_dis_reg() {
	//state change in the power regulators
	if(command_regulator_enabled) enable_reg(); //if we wanted to enable the regulators, enable them
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

