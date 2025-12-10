/*
 * app_debug_protobuf.cpp
 *
 *  Created on: Oct 8, 2025
 *      Author: govis
 */


#include "app_debug_protobuf.hpp"

//constructor - just save the comms system
Debug_Protobuf::Debug_Protobuf(Comms_Subsys& _comms):
	comms(_comms)
{}

//print function
void Debug_Protobuf::print(Msg_t msg) {
	//protobuf temporary
	app_Debug debug_message = app_Debug_init_zero;

	//set the debug level
	debug_message.level = app_Debug_Level_INFO;

	//pop the actual debug text into the message
	uint8_t* dest = reinterpret_cast<uint8_t*>(&debug_message.msg);
	auto text = msg.span();
	std::copy(text.begin(), text.end(), dest);

	//push the debug out to the publish port, push immediately
	comms_debug_inbound.publish_unconditional(debug_message);
	comms.push_messages();
}

//warn function
void Debug_Protobuf::warn(Msg_t msg) {
	//protobuf temporary
	app_Debug debug_message = app_Debug_init_zero;

	//set the debug level
	debug_message.level = app_Debug_Level_WARNING;

	//pop the actual debug text into the message
	uint8_t* dest = reinterpret_cast<uint8_t*>(&debug_message.msg);
	auto text = msg.span();
	std::copy(text.begin(), text.end(), dest);

	//push the debug out to the publish port, push immediately
	comms_debug_inbound.publish_unconditional(debug_message);
	comms.push_messages();
}

//error function
void Debug_Protobuf::error(Msg_t msg) {
	//protobuf temporary
	app_Debug debug_message = app_Debug_init_zero;

	//set the debug level
	debug_message.level = app_Debug_Level_ERROR;

	//pop the actual debug text into the message
	uint8_t* dest = reinterpret_cast<uint8_t*>(&debug_message.msg);
	auto text = msg.span();
	std::copy(text.begin(), text.end(), dest);

	//push the debug out to the publish port, push immediately
	comms_debug_inbound.publish_unconditional(debug_message);
	comms.push_messages();
}
