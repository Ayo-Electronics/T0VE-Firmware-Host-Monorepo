/*
 * app_comms_subsys.cpp
 *
 *  Created on: Oct 7, 2025
 *      Author: govis
 */

#include "app_comms_subsys.hpp"

//========================== PUBLIC FUNCTIONS ==========================

//constructor, just initialize our most important members
Comms_Subsys::Comms_Subsys(USB_Interface& usb_if) :
    cdc_interface(usb_if, CDC_Interface::CDC_CHANNEL)
{}

//init function
void Comms_Subsys::init() {
    //init the CDC interface
    cdc_interface.init();

    //attach a thread signal for when we receive flow control changes
    //use to check connection status
    cdc_interface.register_flow_control_change_cb(BIND_CALLBACK(&flow_control_changed, signal));

    //forward connection request to the state variable handler
    do_command_comms_allow_connections();

    //initialize connection status here too
    status_comms_connected.publish(cdc_interface.connected());

    //and schedule the main thread function
    check_state_update_comms_task.schedule_interval_ms( BIND_CALLBACK(this, check_state_update_comms),
                                                        Scheduler::INTERVAL_EVERY_ITERATION);
}

void Comms_Subsys::push_messages() {
	//transmit inbound debug message
	if(comms_debug_inbound.check(false)) {	//only acknowledge after staging
		//make a quick debug message
		app_Communication msg = app_Communication_init_zero;

		//set its type to debug, copy in the debug message
		msg.which_payload = app_Communication_debug_message_tag;
		msg.payload.debug_message = comms_debug_inbound.read();

		//serialize, transmit, clear signal if indicated by transmit function
		if(serialize_transmit(msg))	comms_debug_inbound.refresh();
	}

	//transmit inbound node state message
	if(comms_node_state_inbound.check(false)) {
		//make a quick debug message
		app_Communication msg = app_Communication_init_zero;

		//set its type to debug, copy in the debug message
		msg.which_payload = app_Communication_node_state_tag;
		msg.payload.node_state = comms_node_state_inbound.read();

		//serialize, transmit, clear signal if indicated by transmit function
		if(serialize_transmit(msg)) comms_node_state_inbound.refresh();
	}

	//and transmit inbound file access response
	if(comms_mem_access_inbound.check(false)) {
		//make a quick debug message
		app_Communication msg = app_Communication_init_zero;

		//set its type to debug, copy in the debug message
		msg.which_payload = app_Communication_neural_mem_request_tag;
		msg.payload.neural_mem_request = comms_mem_access_inbound.read();

		//serialize, transmit, clear signal if indicated by transmit function
		if(serialize_transmit(msg)) comms_mem_access_inbound.refresh();
	}
}

//========================== PRIVATE FUNCTIONS =======================

//main thread function
void Comms_Subsys::check_state_update_comms() {
    //check for any state updates using `available()`
    if(command_comms_allow_connections.check()) {
        //update the CDC interface
        do_command_comms_allow_connections();
    }

    //check our thread signal for flow control changes
    if(flow_control_changed_listener.check()) {
        status_comms_connected.publish(cdc_interface.connected());
    }
    
    //transmit any messages that have been pushed into our inbound variables
    push_messages();

    //run the comms copy/parse/dispatch function every single iteration
    receive_poll();
}

//handler for our command variable
//just forward to the CDC class
void Comms_Subsys::do_command_comms_allow_connections() {
    if(command_comms_allow_connections.read()) {
        cdc_interface.connect_request();
        cdc_interface.set_ready();
    }
    else cdc_interface.disconnect_request();
}

//returns whether we should acknowledge the "data available" signal
bool Comms_Subsys::serialize_transmit(app_Communication& msg) {
	//quick exit path--we're not connected
	if(!cdc_interface.connected()) return true;	//clear our thread signal

	//reset the output stream before each encode (critical!)
	auto stream = pb_ostream_from_buffer(SERZ_BUFFER.data(), SERZ_BUFFER.size());

	//attempt the message serialization
	if(!pb_encode(&stream, app_Communication_fields, &msg)) {
		//encode failed, increment fail counter
		local_serz_err_count++;
		status_comms_encode_err_serz.publish(local_serz_err_count);
		return true; //clear our thread signal
	}

	//serialization successful
	size_t message_size = stream.bytes_written;
	auto active_message = section(SERZ_BUFFER, 0, message_size);

	//validate buffer size
	if(OUTBOUND_DATA.size() < (message_size + HEADER_PADDING)) {
		Debug::WARN("TX: message too large for intermediate buffer!");
		return true;
	}
	std::copy(active_message.begin(), active_message.end(), OUTBOUND_DATA.begin() + HEADER_PADDING);
	OUTBOUND_DATA[0] = START_BYTE;				//drop in start byte
	outbound_data_size = active_message.size();	//drop in size of the active message
	message_size += HEADER_PADDING;

	//and make sure we can fit the message into the transmit FIFO
	if(cdc_interface.tx_bytes_available() < message_size) {
		//we may have a message queued, retry next loop iteration
		//keep thread signal asserted
		return false;
	}

	//finally, drop everything into the transmit buffer; allow clearing of thread signal
	cdc_interface.tx_bytes_write(section(OUTBOUND_DATA, 0, message_size), true);
	return true;
}

//main comms funciton
void Comms_Subsys::receive_poll() {
    //check if we have any bytes available
    //if we don't have any, return
    size_t bytes_available = cdc_interface.rx_bytes_available();
    if(bytes_available == 0) return;

    //if we have some bytes available, copy them into our inbound buffer
    //but make sure to respect buffer sizes
    //update our buffer head apporpriately
    size_t buffer_end = min(inbound_buffer_head + bytes_available, INBOUND_DATA.size());
    size_t bytes_copied = cdc_interface.rx_bytes_read(section(INBOUND_DATA, inbound_buffer_head, buffer_end));
    inbound_buffer_head += bytes_copied;

    //if we haven't found a start byte in our buffer (i.e. our buffer head is < start index)
    //look for it in our buffer
    if(inbound_buffer_head < inbound_start_index) {
        for(size_t i = 0; i < inbound_buffer_head; i++) {
            if(INBOUND_DATA[i] == START_BYTE) {
                inbound_start_index = i;
                break;
            }
        }
    }

    //if we have enough bytes in our buffer to decode the packet length/end index
    //and we haven't done so already (i.e. end_index isn't RESET_VAL), do so now
    //NOTE: treating data size as size of the payload!
    //this allows us to send `0`s over serial to just get state updates
    if( (inbound_start_index != INBOUND_INDEX_RESET) &&                     //we found a valid start index
        (inbound_buffer_head >= inbound_start_index + HEADER_PADDING) &&    //we have enough data to parse an end index
        (inbound_end_index == INBOUND_INDEX_RESET))                         //we haven't done so already
    {
        inbound_data_size.repoint(trim_beg(INBOUND_DATA, inbound_start_index));
        inbound_end_index = inbound_start_index + inbound_data_size.read() + HEADER_PADDING;
    }

    //if we have enough bytes in our buffer to decode the packet, do so now
    if(inbound_buffer_head >= inbound_end_index) {
        //compute the encoded message size, and slice
        //try decoding it, then dispatch it on the appropriate channel
        auto packet_data = section(INBOUND_DATA, inbound_start_index + HEADER_PADDING, inbound_end_index);	//slicing

        //then deserialze/dispatch our message
        deserialize_dispatch(packet_data);

        //and reset our buffer pointer variables
        inbound_start_index = INBOUND_INDEX_RESET;
        inbound_end_index = INBOUND_INDEX_RESET;
        inbound_buffer_head = 0;

        //and finally mark comms activiy
        status_comms_activity.publish(true);
    }

    //if we didn't detect a start byte in this frame and haven't detected one yet
    //reset our buffer head to 0
    if(inbound_buffer_head < inbound_start_index) inbound_buffer_head = 0;

    //if we're overflowing our buffer and haven't completed a message
    //reset our buffer pointer variables
    if(inbound_buffer_head >= BUFFER_SIZE) {
        inbound_start_index = INBOUND_INDEX_RESET;
        inbound_end_index = INBOUND_INDEX_RESET;
        inbound_buffer_head = 0;
    }
}

void Comms_Subsys::deserialize_dispatch(std::span<uint8_t, std::dynamic_extent> msg) {
	//temporary to parse into
	app_Communication message = app_Communication_init_zero;

	//create the protobuf input stream type
	pb_istream_t stream = pb_istream_from_buffer(msg.data(), msg.size());

	//try to decode the message
	if(!pb_decode(&stream, app_Communication_fields, &message)) {
		//decode failure, increment our fail counter, reply with a debug, and abort
		local_deserz_err_count++;
		status_comms_decode_err_deserz.publish(local_deserz_err_count);
		Debug::WARN("RX: Protobuf Deserialization Error!");
		return;
	}

	//dispatch based on message type
	switch(message.which_payload) {
		case app_Communication_node_state_tag:
			comms_node_state_outbound.publish_unconditional(message.payload.node_state);
			break;
		case app_Communication_debug_message_tag:
			comms_debug_outbound.publish_unconditional(message.payload.debug_message);
			break;
		case app_Communication_neural_mem_request_tag:
			comms_mem_access_outbound.publish_unconditional(message.payload.neural_mem_request);
			break;
		default:
			//message type error, increment fail counter, reply with debug
			local_msgtype_err_count++;
			status_comms_decode_err_msgtype.publish(local_msgtype_err_count);
			Debug::WARN("RX: Invalid Protobuf Message Type");
			break;
	}
}
