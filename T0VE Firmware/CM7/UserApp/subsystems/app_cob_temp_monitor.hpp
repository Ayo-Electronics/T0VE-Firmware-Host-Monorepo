/*
 * app_cob_temp_monitor.hpp
 *
 *  Created on: Aug 15, 2025
 *      Author: govis
 */

#pragma once

#include "app_hal_i2c.hpp" //for the I2C bus to pass to the TMP117 class
#include "app_scheduler.hpp" //for the scheduler task
#include "app_state_variable.hpp" //for shared states
#include "app_state_machine_library.hpp" //for enable/disabled state machine

#include "app_tmp117.hpp"

class CoB_Temp_Monitor {
public:
    //constructor--take an I2C bus we'll pass to the TMP117 class
	CoB_Temp_Monitor(Aux_I2C& _bus);

    //initialization routine
    void init();

    //and basically the only other thing we need for this subsystem is getting the state control handles
    //use the macros in the state variable header to generate these easily
    SUBSCRIBE_FUNC(status_device_present);
    SUBSCRIBE_FUNC_RC(status_temp_sensor_error);
    SUBSCRIBE_FUNC(status_temp_sensor_device_id);
    SUBSCRIBE_FUNC(status_cob_temperature_c);
    LINK_FUNC(status_onboard_pgood);

    //delete the copy constructor and assignment operator
    CoB_Temp_Monitor(const CoB_Temp_Monitor& other) = delete;
    void operator=(const CoB_Temp_Monitor& other) = delete;

private:
    //enable/disable function that sets everything up if power is good
	void enable();
	void disable();

    //main thread function for the scheduler to run + associated scheduler
    //basically just updates the temperature state once the read completes
    Scheduler check_state_update_task;
    void check_state_update();

    //scheduler calls this function periodically to get new temperature sensor data
    Scheduler stage_temp_sensor_read_task;
    void stage_temp_sensor_read();
    static const uint32_t TEMP_SENSOR_READ_PERIOD_MS = 125; //matching sample rate of temperature sensor; timing discrepancy not a huge deal

    //call this function if theres an error reading the temperature
    void temp_read_error();

    //run this function when we have new temp sensor data
    //atomic thread signal defers sensor reading from ISR context to main thread context
    Thread_Signal service_temp_sensor_read_SIGNAL = {};
    void service_temp_sensor_read();

    //own a temperature sensor
    TMP117 temp_sensor;
    static constexpr TMP117::TMP117_Configuration_t SENSOR_CONFIG = {	.dev_addr = TMP117::TMP117_ADDR_0x49,
																		.sampling_config = TMP117::TMP117_MODE_CONTINUOUS,
																		.conversion_rate_config = 0, //125ms between samples
																		.averaging_config = TMP117::TMP117_AVERAGING_8,
																		.alert_mode_config = TMP117::TMP117_MODE_ALERT,
																		.alert_polarity_config = TMP117::TMP117_ALERT_ACTIVE_LOW,
																		.alert_source_config = TMP117::TMP117_ALERT_DRDY,			};

    //shared state variables
    State_Variable<bool> status_device_present; //report whether we detected the device during initialization
    State_Variable<bool> status_temp_sensor_error; //asserted when any kinda temperature sensor error happens
    State_Variable<uint16_t> status_temp_sensor_device_id; //report the device ID of the detected temperature sensing
    State_Variable<float> status_cob_temperature_c; //actual temperature reported by the sensor in deg C
    SV_Subscription<bool> status_onboard_pgood; //whether motherboard CoB supplies are up--using onboard supplies as proxy
    //This can technically result in a failure mode where the 3.3V rail fails, but power is still reported as good
    //locks up the I2C bus and potentially back-powers devices (Not really worrying about this failure mode since non-catastrophic and not common)
    //However if we really cared TODO: Fix hardware such that either
    //	a) [MOTHERBOARD_FIX] CoB and DACs are driven by the same supply rail as the arduinos
    //	b) [MOTHERBOARD_FIX] motherboard PGOOD signals are permanently tied to GND for non-aux cards
    //	c) [PROCESSOR_CARD_FIX] processor card DAC runs on different I2C bus as motherboard I2C lines

    //and a really basic state machine to cycle between enabled/disabled depending on PGOOD status
	ESM_State temp_state_ENABLED;
	ESM_State temp_state_DISABLED;
	bool trans_ENABLE_to_DISABLE() { return !status_onboard_pgood; }	//check our subscription variable to see if power is bad
	bool trans_DISABLE_to_ENABLE() { return status_onboard_pgood; }	//check our subscription variable to see if power is good
	ESM_Transition trans_from_ENABLED[1] = {	{&temp_state_DISABLED, {BIND_CALLBACK(this, trans_ENABLE_to_DISABLE)}		}	};
	ESM_Transition trans_from_DISABLED[1] = {	{&temp_state_ENABLED, {BIND_CALLBACK(this, trans_DISABLE_to_ENABLE)}		}	};
	Extended_State_Machine esm;
	Scheduler esm_exec_task; //and a scheduler to call the `run_esm` function
};
