/*
 * app_debug_protobuf.cpp
 *
 *  Created on: Oct 8, 2025
 *      Author: govis
 */


#include "app_debug_protobuf.hpp"

//constructor - just save the comms system
Debug_Protobuf::Debug_Protobuf(Comms_Subsys& _comms) : comms(_comms) {}

//print function
void Debug_Protobuf::print(Msg_t msg) {
	//temporary formatting struct
	App_Communication message = App_Communication_init_zero;

	//say that our message type is gonna be a state update
	message.which_payload = App_Communication_debug_message_tag;

	//mark the message as an info message
	message.payload.debug_message.level = App_Debug_Level_INFO;

	//pop the actual debug text into the message
	uint8_t* dest = reinterpret_cast<uint8_t*>(&message.payload.debug_message.msg);
	auto text = msg.span();
	std::copy(text.begin(), text.end(), dest);

	//encode the message
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer.data(), encode_buffer.size());
	if(pb_encode(&stream, App_Communication_fields, &message)) {
		//if encoding succeeds, drop the message in the transmit fifo of the comms subsystem
		comms.inject(section(encode_buffer, 0, stream.bytes_written));
	}
}

//warn function
void Debug_Protobuf::warn(Msg_t msg) {
	//temporary formatting struct
	App_Communication message = App_Communication_init_zero;

	//say that our message type is gonna be a state update
	message.which_payload = App_Communication_debug_message_tag;

	//mark the message as an info message
	message.payload.debug_message.level = App_Debug_Level_WARNING;

	//pop the actual debug text into the message
	uint8_t* dest = reinterpret_cast<uint8_t*>(&message.payload.debug_message.msg);
	auto text = msg.span();
	std::copy(text.begin(), text.end(), dest);

	//encode the message
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer.data(), encode_buffer.size());
	if(pb_encode(&stream, App_Communication_fields, &message)) {
		//if encoding succeeds, drop the message in the transmit fifo of the comms subsystem
		comms.inject(section(encode_buffer, 0, stream.bytes_written));
	}
}

//error function
void Debug_Protobuf::error(Msg_t msg) {
	//temporary formatting struct
	App_Communication message = App_Communication_init_zero;

	//say that our message type is gonna be a state update
	message.which_payload = App_Communication_debug_message_tag;

	//mark the message as an info message
	message.payload.debug_message.level = App_Debug_Level_ERROR;

	//pop the actual debug text into the message
	uint8_t* dest = reinterpret_cast<uint8_t*>(&message.payload.debug_message.msg);
	auto text = msg.span();
	std::copy(text.begin(), text.end(), dest);

	//encode the message
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer.data(), encode_buffer.size());
	if(pb_encode(&stream, App_Communication_fields, &message)) {
		//if encoding succeeds, drop the message in the transmit fifo of the comms subsystem
		comms.inject(section(encode_buffer, 0, stream.bytes_written));
	}
}
