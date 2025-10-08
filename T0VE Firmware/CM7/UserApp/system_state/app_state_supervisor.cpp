/*
 * app_state_supervisor.cpp
 *
 *  Created on: Aug 18, 2025
 *      Author: govis
 */

#include "app_state_supervisor.hpp"

//protobuf includes
#include "app_messages.pb.h"
#include "pb_common.h"
#include "pb_encode.h"
#include "pb_decode.h"

//constructor
//default initialization of all state variables done in header
State_Supervisor::State_Supervisor() {}

//serialization - pb_encode
std::span<uint8_t, std::dynamic_extent> State_Supervisor::serialize() {
	//temporary formatting struct
	App_Communication message = App_Communication_init_zero;

	//say that our message type is gonna be a state update
	message.which_payload = App_Communication_node_state_tag;

	//TODO: actually update the state

	//encode the message
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer.data(), encode_buffer.size());
	if(!pb_encode(&stream, App_Communication_fields, &message)) {
		//encoding failed, set our encode failure flag, increment our counter
		encode_failure = true;
		encode_failure_count.with([](size_t& d){ d++; }); //atomic increment

		//return an empty span
		return section(encode_buffer, 0, 0);
	}
	else {
		//encode successful, return a view into our encoded buffer
		return section(encode_buffer, 0, stream.bytes_written);
	}
}

//deserialization - pb_decode
void State_Supervisor::deserialize(std::span<uint8_t, std::dynamic_extent> encoded_msg) {
	//temporary to parse into
	App_Communication message = App_Communication_init_zero;

	//create the protobuf input stream type
	pb_istream_t stream = pb_istream_from_buffer(encoded_msg.data(), encoded_msg.size());

	//try to decode the message
	if(pb_decode(&stream, App_Communication_fields, &message)) {
		//decode success, update our state variables
		//TODO
	}
	else {
		//decode failure, update the decode error read-clear flag
		decode_failure = true;
		decode_failure_count.with([](size_t& d){ d++; }); //atomic increment of decode failure count
	}
}






