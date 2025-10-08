/*
 * app_comms_subsys.hpp
 *
 *  Created on: Oct 7, 2025
 *      Author: govis
 *
 *  The
 */

#pragma once

#include "app_utils.hpp"
#include "app_proctypes.hpp"
#include "app_usb_if.hpp"
#include "app_cdc_if.hpp"
#include "app_state_supervisor.hpp"
#include "app_state_variable.hpp"
#include "app_regmap_helpers.hpp"	//to pack and unpack message size into bytestream

//protobuf includes for string printing

class Comms_Subsys {
public:
	//============== TYPEDEFS =============

	static const size_t BUFFER_SIZE = 2048;
	static const uint8_t START_BYTE = 0xEE;
	static const size_t HEADER_PADDING = 3; //start byte + message size (2 bytes)

	//=========== PUBLIC FUNCTIONS ===========

	//like all subsystems, just have a publicly exposed init function
	//this will set up all monitoring threads, and callback functions as necessary
	void init();

	//have a message injection "side-channel"
	//useful for debug messages and the like
	void inject(std::span<uint8_t, std::dynamic_extent> msg);

	//##### STATE VARIABLE ASSOCIATION #####
	SUBSCRIBE_FUNC(status_comms_connected);
	SUBSCRIBE_FUNC_RC(status_comms_activity);
	LINK_FUNC(command_comms_allow_connections);

	//##### CONSTRUCTORS #####
	//takes a reference to a state supervisor
	//and an upstream USB instance
	Comms_Subsys(USB_Interface& usb_if, State_Supervisor& _state_supervisor);

	//delete copy constructor and assignment operator
	Comms_Subsys(const Comms_Subsys& other) = delete;
	void operator=(const Comms_Subsys& other) = delete;

private:
	//own a CDC interface for comms
	CDC_Interface cdc_interface;

	//reference a state supervisor interface
	//to update state, we pass it a serialized state message into its `decode` function
	//and to retrieve state, we call the supervisor's `encode` function
	//NOTE: `decode` won't actually return anything. Decode failure will be marked in a protobuf field
	//		and retrieved during serialization
	State_Supervisor& state_supervisor;

	//thread function that copies, parses, and (potentially) dispatches serialized state updates
	//running in normal thread context (i.e. not ISR) context to maximize thread safety
	void comms_copy_parse_dispatch();

	//inbound buffer variables
	std::array<uint8_t, BUFFER_SIZE> INBOUND_DATA = {0};
	Regmap_Field inbound_data_size = {2, 0, 16, true, INBOUND_DATA}; //repoint to the start index
	static constexpr size_t INBOUND_INDEX_RESET = BUFFER_SIZE;
	size_t inbound_buffer_head = 0;
	size_t inbound_start_index = INBOUND_INDEX_RESET;
	size_t inbound_end_index = INBOUND_INDEX_RESET;

	//outbound buffer variables
	std::array<uint8_t, BUFFER_SIZE> OUTBOUND_DATA = {0};
	Regmap_Field outbound_data_size = {2, 0, 16, true, OUTBOUND_DATA};

	//injection buffer variables
	std::array<uint8_t, BUFFER_SIZE> INJECTION_DATA = {0};
	Regmap_Field injection_data_size = {2, 0, 16, true, INJECTION_DATA};

	//Comms system has some states for itself
	State_Variable<bool> status_comms_connected = {false};
	State_Variable<bool> status_comms_activity = {false};
	SV_Subscription<bool> command_comms_allow_connections;

	//and a little handler to handle our command variable
	void do_command_comms_allow_connections();
	
	//and this is our main comms thread function
	//this will handle state variable management and also run the comms copy/parse/dispatch function
	Thread_Signal flow_control_changed; //use to detect connection changes
	void check_state_update_comms();
	Scheduler check_state_update_comms_task;
};
