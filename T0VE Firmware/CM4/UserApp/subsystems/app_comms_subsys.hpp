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
#include "app_threading.hpp"
#include "app_regmap_helpers.hpp"	//to pack and unpack message size into bytestream

#include "app_messages.pb.h"		//do protobuf decoding in this module
#include "pb.h"
#include "pb_common.h"
#include "pb_decode.h"
#include "pb_encode.h"

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

	//call this fu
	void push_messages();

	//##### STATE VARIABLE ASSOCIATION #####
	SUBSCRIBE_FUNC(		status_comms_connected			);
	SUBSCRIBE_FUNC_RC(	status_comms_activity			);
	LINK_FUNC(			command_comms_allow_connections	);
	SUBSCRIBE_FUNC(		status_comms_decode_err_deserz	);
	SUBSCRIBE_FUNC(		status_comms_decode_err_msgtype	);
	SUBSCRIBE_FUNC(		status_comms_encode_err_serz	);

	//##### IO PORTS ######
	//these would be better as queues but not worth adding that complexity
	SUBSCRIBE_FUNC(	comms_node_state_outbound	);
	SUBSCRIBE_FUNC(	comms_mem_access_outbound	);
	SUBSCRIBE_FUNC(	comms_debug_outbound		);	//unlikely to get associated but adding port anyway
	LINK_FUNC(		comms_node_state_inbound	);
	LINK_FUNC(		comms_mem_access_inbound	);
	LINK_FUNC(		comms_debug_inbound			);

	//##### CONSTRUCTORS #####
	//takes a reference to a state supervisor
	//and an upstream USB instance
	Comms_Subsys(USB_Interface& usb_if);

	//delete copy constructor and assignment operator
	Comms_Subsys(const Comms_Subsys& other) = delete;
	void operator=(const Comms_Subsys& other) = delete;

private:
	//own a CDC interface for comms
	CDC_Interface cdc_interface;

	//thread function that copies, parses, and (potentially) dispatches serialized state updates
	//running in normal thread context (i.e. not ISR) context to maximize thread safety
	void receive_poll();
	void deserialize_dispatch(std::span<uint8_t, std::dynamic_extent> msg);

	//and internal transmit functions (some do the protobuf serialization then transmit
	bool serialize_transmit(app_Communication& msg);

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

	//protobuf serialization buffer
	std::array<uint8_t, BUFFER_SIZE> SERZ_BUFFER = {0};

	//communication ports for deserialized protobuf messages
	PERSISTENT((Pub_Var<app_Node_State>), comms_node_state_outbound);
	Sub_Var<app_Node_State> comms_node_state_inbound;
	PERSISTENT((Pub_Var<app_Neural_Mem_FileRequest>), comms_mem_access_outbound);
	Sub_Var<app_Neural_Mem_FileRequest> comms_mem_access_inbound;
	PERSISTENT((Pub_Var<app_Debug>), comms_debug_outbound);
	Sub_Var<app_Debug> comms_debug_inbound;

	//Comms system has some states for itself
	PERSISTENT((Pub_Var<bool>), status_comms_connected);
	PERSISTENT((Pub_Var<bool>), status_comms_activity);
	Sub_Var<bool> command_comms_allow_connections;
	PERSISTENT((Pub_Var<size_t>), status_comms_decode_err_deserz);	//deserialization decode error
	PERSISTENT((Pub_Var<size_t>), status_comms_decode_err_msgtype);	//incorrect message type
	PERSISTENT((Pub_Var<size_t>), status_comms_encode_err_serz);		//serialization encode error
	size_t local_deserz_err_count = 0;
	size_t local_msgtype_err_count = 0;
	size_t local_serz_err_count = 0;

	//and a little handler to handle our command variable
	void do_command_comms_allow_connections();
	
	//and this is our main comms thread function
	//this will handle state variable management and also run the comms copy/parse/dispatch function
	PERSISTENT((Thread_Signal), flow_control_changed); //use to detect connection changes
	Thread_Signal_Listener flow_control_changed_listener = flow_control_changed.listen();
	void check_state_update_comms();
	Scheduler check_state_update_comms_task;
};
