/*
 * app_debug_protobuf.hpp
 *
 *  Created on: Oct 8, 2025
 *      Author: govis
 *
 *  Print debug messages out using the protobuf interface
 */

#pragma once

#include "app_comms_subsys.hpp"
#include "app_debug_if.hpp"
#include "app_string.hpp"

//### protobuf includes ###
#include "app_messages.pb.h"
#include "pb.h"
#include "pb_common.h"
#include "pb_encode.h"

class Debug_Protobuf : public Debug_Interface {
public:
	//====================== CONSTRUCTORS ======================
	//construct by taking access to a comms interface
	//we'll send the protobuf messages to this via the `inject` method
	Debug_Protobuf(Comms_Subsys& _comms);

	//and implement the debug interface
	void print(Msg_t msg) override;
	void warn(Msg_t msg) override;
	void error(Msg_t msg) override;
private:
	//reference a comms interface
	Comms_Subsys& comms;

	//and a little permanent buffer to drop encoded messages
	//should only be used locally, but means we don't need a ton of stack space
	static constexpr size_t ENCODE_BUFFER_SIZE = 2048;
	std::array<uint8_t, ENCODE_BUFFER_SIZE> encode_buffer;
};
