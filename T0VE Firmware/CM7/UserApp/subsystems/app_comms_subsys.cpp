/*
 * app_comms_subsys.cpp
 *
 *  Created on: Oct 7, 2025
 *      Author: govis
 */

#include "app_comms_subsys.hpp"

//========================== PUBLIC FUNCTIONS ==========================

//constructor, just initialize our most important members
Comms_Subsys::Comms_Subsys(USB_Interface& usb_if, State_Supervisor& _state_supervisor) : 
    cdc_interface(usb_if, CDC_Interface::CDC_CHANNEL),  //use the default CDC channel
    state_supervisor(_state_supervisor)
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
    status_comms_connected = cdc_interface.connected();

    //and schedule the main thread function
    check_state_update_comms_task.schedule_interval_ms( BIND_CALLBACK(this, check_state_update_comms),
                                                        Scheduler::INTERVAL_EVERY_ITERATION);
}

//a little side channel injection of debug messages
void Comms_Subsys::inject(std::span<uint8_t, std::dynamic_extent> msg) {
	//if we're connected and can fit the message in the TX FIFO, send the message
	if(	cdc_interface.connected() &&
		(cdc_interface.tx_bytes_available() > msg.size()) &&
		(msg.size() + HEADER_PADDING < INJECTION_DATA.size()))
	{
		//drop the message into the injection data buffer
		std::copy(msg.begin(), msg.end(), INJECTION_DATA.begin() + HEADER_PADDING);

		//add padding, add message size
		INJECTION_DATA[0] = START_BYTE;
		injection_data_size = msg.size();

		//and drop into the tx buffer
		cdc_interface.tx_bytes_write(section(INJECTION_DATA, 0, injection_data_size + HEADER_PADDING), true);
	}
}

//========================== PRIVATE FUNCTIONS =======================

//main thread function
void Comms_Subsys::check_state_update_comms() {
    //check for any state updates using `available()`
    if(command_comms_allow_connections.available()) {
        //update the CDC interface
        do_command_comms_allow_connections();
    }

    //check our thread signal for flow control changes
    if(flow_control_changed.available()) {
        status_comms_connected = cdc_interface.connected();
    }
    
    //run the comms copy/parse/dispatch function every single iteration
    comms_copy_parse_dispatch();
}

//handler for our command variable
//just forward to the CDC class
void Comms_Subsys::do_command_comms_allow_connections() {
    if(command_comms_allow_connections) {
        cdc_interface.connect_request();
        cdc_interface.set_ready();
    }
    else cdc_interface.disconnect_request();
}

//main comms funciton
void Comms_Subsys::comms_copy_parse_dispatch() {
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
        //if we have a non-zero length message, then decode/update state, and return new state
        //zero-length messages are treated as "get state" requests, and don't go through the decode pipeline
        auto packet_data = section(INBOUND_DATA, inbound_start_index + HEADER_PADDING, inbound_end_index);	//slicing
        if(packet_data.size() > 0) state_supervisor.deserialize(packet_data);								//decoding/update state
        auto encoded_packet = state_supervisor.serialize();                                                 //encoding the new state          
        
        //copy encoded state into our outbound buffer, respecting buffer sizes for safety
        //set the message header and size appropriately
        size_t encoded_packet_copy_size = min(encoded_packet.size(), OUTBOUND_DATA.size() - HEADER_PADDING);
        std::copy(encoded_packet.begin(), encoded_packet.begin() + encoded_packet_copy_size, OUTBOUND_DATA.begin() + HEADER_PADDING);
        OUTBOUND_DATA[0] = START_BYTE;
        outbound_data_size = encoded_packet_copy_size;

        //write the outbound packet to the CDC interface
        cdc_interface.tx_bytes_write(section(OUTBOUND_DATA, 0, encoded_packet_copy_size + HEADER_PADDING), true);

        //and reset our buffer pointer variables
        inbound_start_index = INBOUND_INDEX_RESET;
        inbound_end_index = INBOUND_INDEX_RESET;
        inbound_buffer_head = 0;

        //and finally mark comms activiy
        status_comms_activity = true;
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
