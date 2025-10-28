/*
 * app_power_monitor.hpp
 *
 *  Created on: Jun 25, 2025
 *      Author: govis
 */

#pragma once

#include "app_proctypes.hpp"
#include "app_utils.hpp"
#include "app_hal_gpio.hpp"
#include "app_scheduler.hpp"

#include "app_threading.hpp"

class Power_Monitor {
public:
    Power_Monitor(  const GPIO::GPIO_Hardware_Pin _pwr_reg_en, const GPIO::GPIO_Hardware_Pin _pwr_pgood,
    				uint32_t _debounce_time_ms, bool _ENABLE_POL_INVERTED = false, bool _PGOOD_POL_INVERTED = true);

    //call this function to initialize the GPIO pins
    void init();

    //and call these functions to get immediate/debounced power status
    //trying to inline `get_immediate_power_status()` for performance reasons
	#pragma GCC push_options
	#pragma GCC optimize ("Ofast,unroll-loops,inline-functions")

    __attribute__((always_inline)) inline bool get_immediate_power_status() {
    	if (PGOOD_POL_INVERTED) return !pwr_pgood.read();
    	else return pwr_pgood.read();
    }
    __attribute__((always_inline)) inline bool get_debounced_power_status() { return local_debounced_power_status; }

	#pragma GCC pop_options

    //use these functions to register callback functions for power events
    void register_power_became_good(Callback_Function<> callback, bool _enable_callbacks = true);
    void register_power_became_bad(Callback_Function<> callback, bool _enable_callbacks = true);
    void enable_callbacks();
    void disable_callbacks();

    //use these functions to get access to the command and status state variables relevant to this system
    //use the macros in the state variable class to generate these easily
    SUBSCRIBE_FUNC(status_immedate_power);
    SUBSCRIBE_FUNC(status_debounced_power);
	LINK_FUNC(command_regulator_enabled);

private:
    //NOTE: MAKING THESE FUNCTIONS PRIVATE--INTERACT WITH THIS SUBSYSTEM PURELY THROUGH PUB/SUB MESSAGES!
	//call these functions to enable/disable the power regulator
	void enable_reg();
	void disable_reg();
	void do_en_dis_reg(); //performing regulator enable command update

    //shared state variables - these will get piped to the comms system
    PERSISTENT((Pub_Var<bool>), status_immedate_power);	//power not good on start
    PERSISTENT((Pub_Var<bool>), status_debounced_power); 	//power not good on start
    Sub_Var<bool> command_regulator_enabled;

    //internal function that checks the power status of the regulator
    void check_power_status();
    //internal function that reads any changes of the state variables
    void check_state_update();
    
    //hold variables related to IO polarity
    const bool ENABLE_POL_INVERTED;
    const bool PGOOD_POL_INVERTED;

    //hold variables related button bouncing
    //constants are set up such that debounce will occur in two calls to the check_power_status function
    uint32_t debounce_time_ms;
    static constexpr float THRESHOLD_HIGH = 0.70;
    static constexpr float THRESHOLD_LOW = 0.30;
    static constexpr float MOVING_AVERAGE_DECAY_FACTOR = 0.5;
    float debounce_average = 0;
    bool local_debounced_power_status = false;	//lightweight local copy, published to the state variable in state update

    //own the GPIO pins related to these
    GPIO pwr_reg_en;
    GPIO pwr_pgood;

    //create a scheduler task to monitor the power status
    //and a task that monitors for state changes in the variables
    Scheduler check_power_status_task = Scheduler();
    Scheduler check_state_update_task = Scheduler();
};
