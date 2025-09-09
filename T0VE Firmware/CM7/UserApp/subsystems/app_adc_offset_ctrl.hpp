/*
 * app_adc_offset_ctrl.hpp
 *
 *  Created on: Aug 15, 2025
 *      Author: govis
 */

#pragma once

#include "app_hal_i2c.hpp" //for the I2C bus to pass to the MCP4728 class
#include "app_scheduler.hpp" //for the scheduler task
#include "app_state_variable.hpp" //for shared states
#include "app_state_machine_library.hpp" //for basic state machine for power good

#include "app_mcp4728.hpp"

class ADC_Offset_Control {
public:
    //constructor--take an I2C bus we'll pass to the MCP4728 class
    ADC_Offset_Control(Aux_I2C& _bus);

    //initialization routine
    void init();

    //and basically the only other thing we need for this subsystem is getting the state control handles
    //use the macros in the state variable header to generate these easily
    SUBSCRIBE_FUNC(status_device_present);
    SUBSCRIBE_FUNC(status_offset_dac_values_readback);
    SUBSCRIBE_FUNC_RC(status_offset_dac_error);
    LINK_FUNC(command_offset_dac_values);
    LINK_FUNC_RC(command_offset_dac_read_update);
    LINK_FUNC(status_onboard_pgood);

    //delete the assignment operator and copy constructor
    ADC_Offset_Control(const ADC_Offset_Control& other) = delete;
    void operator=(const ADC_Offset_Control& other) = delete;

private:
    //enable/disable function that sets everything up if power is good
    void enable();
    void disable();

    //function for the scheduler to run
    void check_state_update();
    
    //functions that gets called when there was a recent transmission error
    void write_error();
    void read_error(); //differentiating so we can acknowledge that a read was completed (though unsuccessful)

    //run this function when we have new DAC data
    //atomic thread signal defers DAC reading from ISR context to main thread context
    Thread_Signal service_offset_dac_read_SIGNAL = {};
    void service_offset_dac_read();

    //functions that actually take care of the I2C communication
    //implementing like this so we can easily defer function calls if the I2C bus is busy
    void do_write_offset_dac_values();
    void do_read_offset_dac_values();

    //own an offset DAC
    MCP4728 offset_dac;

    //shared state variables
    State_Variable<bool> status_device_present; //report whether we detected the device during initialization
    State_Variable<std::array<uint16_t, 4>> status_offset_dac_values_readback; //what the DAC thinks we've written to it
    State_Variable<bool> status_offset_dac_error; //asserted when any kinda dac error happens
    SV_Subscription<std::array<uint16_t, 4>> command_offset_dac_values; //values we'd like to write to the DAC
    SV_Subscription_RC<bool> command_offset_dac_read_update; //asserted when we want to perform a DAC read, cleared after read
    SV_Subscription<bool> status_onboard_pgood; //whether motherboard CoB supplies are up--using onboard supplies as proxy
    //This can technically result in a failure mode where the 3.3V rail fails, but power is still reported as good
	//locks up the I2C bus and potentially back-powers devices (Not really worrying about this failure mode since non-catastrophic and not common)
	//However if we really cared TODO: Fix hardware such that either
	//	a) [MOTHERBOARD_FIX] CoB and DACs are driven by the same supply rail as the arduinos
	//	b) [MOTHERBOARD_FIX] motherboard PGOOD signals are permanently tied to GND for non-aux cards
	//	c) [PROCESSOR_CARD_FIX] processor card DAC runs on different I2C bus as motherboard I2C lines

    //a scheduler task to poll for state updates
    Scheduler check_state_update_task;

    //and scheduler tasks to execute reading/writing to the offset dac
    Scheduler write_offset_dac_values_task;
    Scheduler read_offset_dac_values_task;
    static const uint32_t READ_OFFSET_DAC_VALUES_PERIOD_MS = 250; //automatically pull in the offset DAC values at this rate

    //and a really basic state machine to cycle between enabled/disabled depending on PGOOD status
    ESM_State offset_state_ENABLED;
    ESM_State offset_state_DISABLED;
    bool trans_ENABLE_to_DISABLE() { return !status_onboard_pgood; }	//check our subscription variable to see if power is bad
	bool trans_DISABLE_to_ENABLE() { return status_onboard_pgood; }	//check our subscription variable to see if power is good
	ESM_Transition trans_from_ENABLED[1] = {	{&offset_state_DISABLED, {BIND_CALLBACK(this, trans_ENABLE_to_DISABLE)}		}	};
	ESM_Transition trans_from_DISABLED[1] = {	{&offset_state_ENABLED, {BIND_CALLBACK(this, trans_DISABLE_to_ENABLE)}		}	};
	Extended_State_Machine esm;
	Scheduler esm_exec_task; //and a scheduler to call the `run_esm` function
};
