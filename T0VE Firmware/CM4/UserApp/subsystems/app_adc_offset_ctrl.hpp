/*
 * app_adc_offset_ctrl.hpp
 *
 *  Created on: Aug 15, 2025
 *      Author: govis
 */

#pragma once

#include "app_hal_i2c.hpp" //for the I2C bus to pass to the MCP4728 class
#include "app_scheduler.hpp" //for the scheduler task
#include "app_state_machine_library.hpp" //for basic state machine for power good
#include "app_threading.hpp" //for pub/sub var

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

    //thread signals and associated listeners for deferring interrupts to normal program context
    PERSISTENT((Thread_Signal), write_error_flag);
    Thread_Signal_Listener write_error_listener = write_error_flag.listen();
    PERSISTENT((Thread_Signal), read_complete_flag);
    Thread_Signal_Listener read_complete_listener = read_complete_flag.listen();
    PERSISTENT((Thread_Signal), read_error_flag);
    Thread_Signal_Listener read_error_listener = read_error_flag.listen();

    //read/write functions
    void do_write_offset_dac_values();	//stage a write, assume it went through unless error signaled
    void do_read_offset_dac_values();	//stage a read
    void service_offset_dac_read();		//read completed, we have new data

    //some signals to poll the DACs periodicaly
    Scheduler dac_periodic_read;
    static const uint32_t READ_OFFSET_DAC_VALUES_PERIOD_MS = 1000; //automatically pull in the offset DAC values at this rate
    PERSISTENT((Thread_Signal), do_read_signal);	//use this to defer DAC reads too
    Thread_Signal_Listener do_read_listener = do_read_signal.listen();
    PERSISTENT((Thread_Signal), do_write_signal);	//use this to defer DAC writes
    Thread_Signal_Listener do_write_listener = do_write_signal.listen();

    //own an offset DAC
    MCP4728 offset_dac;

    //shared state variables
    PERSISTENT((Pub_Var<bool>), status_device_present);									//report whether we detected the device during initialization
    PERSISTENT((Pub_Var<std::array<uint16_t, 4>>), status_offset_dac_values_readback);	//what the DAC thinks we've written to it
    PERSISTENT((Pub_Var<bool>), status_offset_dac_error);								//asserted when any kinda dac error happens
    Sub_Var<std::array<uint16_t, 4>> command_offset_dac_values; //values we'd like to write to the DAC
    Sub_Var_RC<bool> command_offset_dac_read_update; //asserted when we want to perform a DAC read, cleared after read
    Sub_Var<bool> status_onboard_pgood; //whether motherboard CoB supplies are up--using onboard supplies as proxy
    //This can technically result in a failure mode where the 3.3V rail fails, but power is still reported as good
	//locks up the I2C bus and potentially back-powers devices (Not really worrying about this failure mode since non-catastrophic and not common)
	//However if we really cared TODO: Fix hardware such that either
	//	a) [MOTHERBOARD_FIX] CoB and DACs are driven by the same supply rail as the arduinos
	//	b) [MOTHERBOARD_FIX] motherboard PGOOD signals are permanently tied to GND for non-aux cards
	//	c) [PROCESSOR_CARD_FIX] processor card DAC runs on different I2C bus as motherboard I2C lines

    //a scheduler task to poll for state updates and its associated function
    Scheduler check_state_update_task;
    void check_state_update();

    //and a really basic state machine to cycle between enabled/disabled depending on PGOOD status
    ESM_State offset_state_ENABLED;
    ESM_State offset_state_DISABLED;
    bool trans_ENABLE_to_DISABLE() { return !status_onboard_pgood.read(); }	//check our subscription variable to see if power is bad
	bool trans_DISABLE_to_ENABLE() { return status_onboard_pgood.read(); }	//check our subscription variable to see if power is good
	ESM_Transition trans_from_ENABLED[1] = {	{&offset_state_DISABLED, {BIND_CALLBACK(this, trans_ENABLE_to_DISABLE)}		}	};
	ESM_Transition trans_from_DISABLED[1] = {	{&offset_state_ENABLED, {BIND_CALLBACK(this, trans_DISABLE_to_ENABLE)}		}	};
	Extended_State_Machine esm;
	Scheduler esm_exec_task; //and a scheduler to call the `run_esm` function
};
