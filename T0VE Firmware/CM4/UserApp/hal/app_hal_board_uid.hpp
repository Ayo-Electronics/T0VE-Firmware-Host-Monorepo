/*
 * app_hal_board_uid.hpp
 *
 *  Created on: Oct 23, 2025
 *      Author: govis
 */

#pragma once

//note the destination address of the UID,
//expose in a C-compatible header way
#define PUBLIC_SHARED_UID_ADDRESS 0x38000000

#ifdef __cplusplus

#include "app_proctypes.hpp"
#include "app_string.hpp"

class Board_UID {
public:
	//nothing in the constructor
	Board_UID() {}

	//nothing in init really
	void init() {}

	//return string version of UID in hex
	auto uid_string() {
		auto bytes = uid_bytes();						//get the UID bytes of the system
		std::array<uint8_t, bytes.size() * 2> uid_hex;	//and an output temporary for the hex

		//go through the bytes turn them into a hexadecimal string
		for(size_t i = 0; i < bytes.size(); i++) {
			const char to_hex[] = "0123456789ABCDEF";
			uint8_t upper_nibble = (bytes[i] >> 4) & 0x0F;
			uint8_t lower_nibble = (bytes[i] >> 0) & 0x0F;
			uid_hex[2*i] = 		to_hex[upper_nibble];
			uid_hex[2*i + 1] = 	to_hex[lower_nibble];
		}

		//return an app string of the correct size
		return App_String<uid_hex.size()>(uid_hex);
	}

	//return byte array version of UID (128-bits); zero pad as necessary
	std::array<uint8_t, 16> uid_bytes() {
		std::array<uint8_t, 16> out = {0};	//zero initialize

		//for CM4, don't have access to the UID registers
		//rely on the CM7 publishing the UID information
		memcpy(out.data(), reinterpret_cast<void*>(PUBLIC_SHARED_UID_ADDRESS), 96/8);
		return out;
	}

private:
};

#endif
