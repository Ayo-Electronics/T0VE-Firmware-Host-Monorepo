/*
 * app_bias_drives.hpp
 *
 *  Created on: Oct 1, 2025
 *      Author: govis
 */

#pragma once

#include "app_ad5675.hpp"
#include "app_utils.hpp"
#include "app_proctypes.hpp"
#include "app_state_machine_library.hpp"
#include "app_hal_i2c.hpp"
#include "app_state_variable.hpp"
#include "app_scheduler.hpp"
#include "app_hal_gpio.hpp"
#include "app_hal_pin_mapping.hpp"

class Waveguide_Bias_Drive {
public:
	//================ TYPEDEFS AND CONSTANTS =================

	struct Waveguide_Bias_Setpoints_t {
		std::array<uint16_t, 2> bulk_setpoints;
		std::array<uint16_t, 4> mid_setpoints;
		std::array<uint16_t, 10> stub_setpoints;

		//need to define explicity equality operator (equality comparison called in atomic variable)
		bool operator==(const Waveguide_Bias_Setpoints_t& other) const {
		return bulk_setpoints == other.bulk_setpoints &&
			   mid_setpoints  == other.mid_setpoints  &&
			   stub_setpoints == other.stub_setpoints;
		}
	};

	//=================== PUBLIC FUNCTIONS ===================

	//constructor--take an I2C bus we'll pass to the AD5675 class
	//along with GPIOs for the regulator enable and DAC reset pins
	Waveguide_Bias_Drive(	Aux_I2C& _bus,
							const GPIO::GPIO_Hardware_Pin& _reg_enable_pin,
							const GPIO::GPIO_Hardware_Pin& _dac_reset_pin);

	//initialization routine
	void init();

	//and basically the only other thing we need for this subsystem is getting the state control handles
	//use the macros in the state variable header to generate these easily
	SUBSCRIBE_FUNC(status_device_present);
	SUBSCRIBE_FUNC(status_bias_dac_values_readback);
	SUBSCRIBE_FUNC_RC(status_bias_dac_error);
	LINK_FUNC_RC(command_bias_dac_values);
	LINK_FUNC_RC(command_bias_reg_enable);
	LINK_FUNC_RC(command_bias_dac_read_update);
	LINK_FUNC(status_motherboard_pgood);

	//delete the assignment operator and copy constructor
	Waveguide_Bias_Drive(const Waveguide_Bias_Drive& other) = delete;
	void operator=(const Waveguide_Bias_Drive& other) = delete;

private:
	//shared state variables
	State_Variable<bool> status_device_present; 								//report whether we detected the device during initialization
	State_Variable<Waveguide_Bias_Setpoints_t> status_bias_dac_values_readback;	//what the DAC thinks we've written to it
	State_Variable<bool> status_bias_dac_error; 								//asserted when any kinda dac error happens, expose read-clear port
	SV_Subscription_RC<Waveguide_Bias_Setpoints_t> command_bias_dac_values; 	//values we'd like to write to the DAC
	SV_Subscription_RC<bool> command_bias_reg_enable;							//enable the actual voltage regulators for the waveguide bias system
	SV_Subscription_RC<bool> command_bias_dac_read_update; 						//asserted when we want to perform a DAC read, cleared after read
	SV_Subscription<bool> status_motherboard_pgood; 							//whether motherboard CoB supplies are up

	//################################### DAC INTERFACE HELPERS ########################################
	//class controls two AD5675s
	//labelling them based off their I2C addresses
	AD5675 bias_dac_0x0C;
	AD5675 bias_dac_0x0F;

	//have an array of structs that map a particular bias drive channel to a particular DAC + channel
	struct Waveguide_Bias_Mapping_t {
		AD5675* dac;
		uint8_t channel;
	};

	//and have a collection that augments a mapping with a set of actual channel-wise value
	struct Bias_Setpoints_t {
		Waveguide_Bias_Mapping_t mapping;
		uint16_t channel_val;
		static void update_collection(	std::array<Bias_Setpoints_t, 16>& setpoints,
										const std::array<Waveguide_Bias_Mapping_t, 2>& bulk_mapping,
										const std::array<Waveguide_Bias_Mapping_t, 4>& mid_mapping,
										const std::array<Waveguide_Bias_Mapping_t, 10>& stub_mapping,
										const Waveguide_Bias_Setpoints_t new_setpoints)
		{
			//indexing variable
			size_t indexer = 0;

			//quick lambda function to help out data placement into the correct array
			auto place_data = [&](	std::span<const uint16_t, std::dynamic_extent> new_values,
									std::span<const Waveguide_Bias_Mapping_t, std::dynamic_extent> mapping)
			{
				//go through all items in the array
				for(size_t i = 0; i < mapping.size(); i++) {
					Bias_Setpoints_t sp = {.mapping = mapping[i], .channel_val = new_values[i]};
					setpoints[indexer] = sp;
					indexer++;
				}
			};

			//and place all the data in our structure
			place_data(new_setpoints.bulk_setpoints, bulk_mapping);
			place_data(new_setpoints.mid_setpoints, mid_mapping);
			place_data(new_setpoints.stub_setpoints, stub_mapping);
		}
	};

	//and have our channel-wise mappings
	const std::array<Waveguide_Bias_Mapping_t, 2> bulk_mapping = {{
			{&bias_dac_0x0F, 2},
			{&bias_dac_0x0F, 3}
	}};
	const std::array<Waveguide_Bias_Mapping_t, 4> mid_mapping = {{
			{&bias_dac_0x0F, 5},
			{&bias_dac_0x0F, 4},
			{&bias_dac_0x0F, 1},
			{&bias_dac_0x0F, 0}
	}};
	const std::array<Waveguide_Bias_Mapping_t, 10> stub_mapping = {{
			{&bias_dac_0x0F, 6},
			{&bias_dac_0x0F, 7},
			{&bias_dac_0x0C, 2},
			{&bias_dac_0x0C, 3},
			{&bias_dac_0x0C, 5},
			{&bias_dac_0x0C, 4},
			{&bias_dac_0x0C, 1},
			{&bias_dac_0x0C, 0},
			{&bias_dac_0x0C, 6},
			{&bias_dac_0x0C, 7}
	}};

	//################################### DAC WRITING STATE MACHINE ########################################
	//state variables necessary for writing flow control
	std::array<Bias_Setpoints_t, 16> bias_setpoints;
	size_t bias_setpoint_tx_index = 0;
	bool tx_transfer_staged = false;
	Thread_Signal write_data_do;
	Thread_Signal write_error_signal;

	//function called on write transmission error
	void write_error();

	//and have a state machine that manages the writing functionality
	//have three states, IDLE, TX, INCREMENT and transitions between them
	ESM_State bias_tx_state_IDLE;
	ESM_State bias_tx_state_TX;
	ESM_State bias_tx_state_INCREMENT;
	bool trans_IDLE_to_TX() { return 	command_bias_dac_values.available() ||
										write_data_do.available(); } 								//drop into TX mode if we want to update our bias dac values (via user or internal signal)
	bool trans_TX_to_INCREMENT() { return tx_transfer_staged && !write_error_signal.available(); }	//increment our setpoint tx index if our transmission was staged successfully
	bool trans_TX_to_IDLE() { return write_error_signal.available(); }								//return from tx to idle if we have a transmission error
	bool trans_INCREMENT_to_TX() { return bias_setpoint_tx_index < bias_setpoints.size(); };		//return from increment to tx if we have more data to transmit
	bool trans_INCREMENT_to_IDLE() { return bias_setpoint_tx_index >= bias_setpoints.size(); }		//return from increment to idle if we've transmitted our entire buffer (tx error will be autonomously set)

	//provide hooks for our thread functions
	void tx_IDLE_on_exit();			//update setpoints container, reset counter, clear error signals
	void tx_TX_thread_func();		//try to stage a transmission
	void tx_INCREMENT_on_entry();	//increment our tx buffer pointer

	//aggregate our state transitions and create our ESM
	ESM_Transition tx_trans_from_IDLE[1] =	{		{&bias_tx_state_TX, {BIND_CALLBACK(this, trans_IDLE_to_TX)}				}	};
	ESM_Transition tx_trans_from_TX[2] = {			{&bias_tx_state_INCREMENT, {BIND_CALLBACK(this, trans_TX_to_INCREMENT)}	},
													{&bias_tx_state_IDLE, {BIND_CALLBACK(this, trans_TX_to_IDLE)}			}	};
	ESM_Transition tx_trans_from_INCREMENT[2] = {	{&bias_tx_state_TX, {BIND_CALLBACK(this, trans_INCREMENT_to_TX)}		},
													{&bias_tx_state_IDLE, {BIND_CALLBACK(this, trans_INCREMENT_to_IDLE)}	}	};
	Extended_State_Machine esm_tx;

	//################################### DAC READING STATE MACHINE ########################################
	//state variables necessary for reading flow control
	static const uint32_t READ_BIAS_DAC_VALUES_PERIOD_MS = 1000; //automatically pull in the bias DAC values at this rate
	Scheduler periodic_read_task;	//scheduler that sets the read thread signal periodically
	Thread_Signal read_data_do;		//for our periodic reading
	Thread_Signal read_complete;
	Thread_Signal read_error_signal;
	bool rx_transfer_success;

	//function called on read transmission error
	void read_error();

	//have a couple states
	//IDLE, REQUEST_1, WAIT1, REQUEST_2, WAIT2, READBACK_UPDATE
	//and some transitions in between them (will basically be sequential execution)
	ESM_State bias_rx_state_IDLE;
	ESM_State bias_rx_state_REQUEST1;
	ESM_State bias_rx_state_WAIT1;
	ESM_State bias_rx_state_REQUEST2;
	ESM_State bias_rx_state_WAIT2;
	ESM_State bias_rx_state_RBUPDATE;
	bool trans_IDLE_to_REQUEST1() 		{ return read_data_do.available() || command_bias_dac_read_update; }	//interval read or user requested
	bool trans_REQUEST1_to_WAIT1() 		{ return rx_transfer_success && !read_error_signal.available(); }		//after first transmission staged successfully
	bool trans_REQUEST1_to_IDLE()		{ return read_error_signal.available(); }								//if error staging first transmission
	bool trans_WAIT1_to_REQUEST2()		{ return read_complete.available() && !read_error_signal.available(); }	//move to second request if the read has completed
	bool trans_WAIT1_to_IDLE()			{ return read_error_signal.available(); }								//move back to idle if there was an error during transmission
	bool trans_REQUEST2_to_WAIT2() 		{ return rx_transfer_success && !read_error_signal.available(); }		//after second transmission staged successfully
	bool trans_REQUEST2_to_IDLE() 		{ return read_error_signal.available(); }								//if error staging second transmission
	bool trans_WAIT2_to_RBUPDATE()		{ return read_complete.available() && !read_error_signal.available(); }	//move to readback update, read 2 complete
	bool trans_WAIT2_to_IDLE()			{ return read_error_signal.available(); }	//if error after second transmission
	bool trans_RBUPDATE_to_IDLE() 		{ return true; }							//always return to idle after unpacking data

	//provide hooks for our thread functions
	void rx_IDLE_on_exit();			//clear all signal variables
	void rx_REQUEST1_thread_func();	//try to stage a transmission from the first DAC
	void rx_REQUEST2_thread_func();	//try to stage a transmission from the second DAC
	void rx_RBUPDATE_on_entry();	//state variable updating from DAC readbacks, acknowledge read complete

	//aggregate our transitions and create our esm
	ESM_Transition rx_trans_from_IDLE[1] =	{		{&bias_rx_state_REQUEST1, {BIND_CALLBACK(this, trans_IDLE_to_REQUEST1)}		}	};
	ESM_Transition rx_trans_from_REQUEST1[2] = {	{&bias_rx_state_WAIT1, {BIND_CALLBACK(this, trans_REQUEST1_to_WAIT1)}		},
													{&bias_rx_state_IDLE, {BIND_CALLBACK(this, trans_REQUEST1_to_IDLE)}			}	};
	ESM_Transition rx_trans_from_WAIT1[2] = {		{&bias_rx_state_REQUEST2, {BIND_CALLBACK(this, trans_WAIT1_to_REQUEST2)}	},
													{&bias_rx_state_IDLE, {BIND_CALLBACK(this, trans_WAIT1_to_IDLE)}			}	};
	ESM_Transition rx_trans_from_REQUEST2[2] = {	{&bias_rx_state_WAIT2, {BIND_CALLBACK(this, trans_REQUEST2_to_WAIT2)}			},
													{&bias_rx_state_IDLE, {BIND_CALLBACK(this, trans_REQUEST2_to_IDLE)}			}	};
	ESM_Transition rx_trans_from_WAIT2[2] = {		{&bias_rx_state_RBUPDATE, {BIND_CALLBACK(this, trans_WAIT2_to_RBUPDATE)}		},
													{&bias_rx_state_IDLE, {BIND_CALLBACK(this, trans_WAIT2_to_IDLE)}			}	};
	ESM_Transition rx_trans_from_RBUPDATE[1] =	{	{&bias_rx_state_IDLE, {BIND_CALLBACK(this, trans_RBUPDATE_to_IDLE)}			}	};
	Extended_State_Machine esm_rx;

	//########################################## DAC ENABLE/DISABLE STATE MACHINE #################################################
	//enable/disable function that sets everything up if power is good/bad
	void enable();
	void disable();

	//and a really basic state machine to cycle between enabled/disabled depending on PGOOD status
	ESM_State bias_state_ENABLED;
	ESM_State bias_state_DISABLED;
	bool trans_ENABLE_to_DISABLE() { return !status_motherboard_pgood; }	//check our subscription variable to see if power is bad
	bool trans_DISABLE_to_ENABLE() { return status_motherboard_pgood; }		//check our subscription variable to see if power is good
	ESM_Transition trans_from_ENABLED[1] = {	{&bias_state_DISABLED, {BIND_CALLBACK(this, trans_ENABLE_to_DISABLE)}		}	};
	ESM_Transition trans_from_DISABLED[1] = {	{&bias_state_ENABLED, {BIND_CALLBACK(this, trans_DISABLE_to_ENABLE)}		}	};
	Extended_State_Machine esm_supervisor;

	//########################################### SCHEDULER FOR ESM TASKS ###########################################
	Scheduler esm_supervisor_task;

	inline void run_tx_rx_esm() { esm_tx.RUN_ESM(); esm_rx.RUN_ESM(); } //inline helper to run the tx/rx state machines
	Scheduler esm_tx_rx_task;

	//########################################### GPIO CONTROL ###########################################
	GPIO dac_reset_line;	//GPIO pin used to reset the DAC pins
	GPIO reg_enable;		//GPIO pin used to enable the waveguide bias regulators

	//functions relevant to regulator GPIO control management
	//	- one function actually performs GPIO control based on command variable
	//	- one function checks if the command value changed
	//	- a scheduler runs the "check" function
	void do_regulator_ctrl_update();
	inline void check_regulator_ctrl_update() { if(command_bias_reg_enable.available()) do_regulator_ctrl_update(); }
	Scheduler check_regulator_ctrl_update_task;
};
