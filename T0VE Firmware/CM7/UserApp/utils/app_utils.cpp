/*
 * utils.cpp
 *
 *  Created on: Sep 20, 2023
 *      Author: Ishaan
 */

#include "app_utils.hpp"

#include <bit> //for bit_cast

//=========================================== BYTE PACKING/UNPACKING ===========================================

//pack a uint32_t value in big endian order
void pack(const uint32_t val, std::span<uint8_t, std::dynamic_extent> buf) {
	buf[0] = (uint8_t)(0xFF & (val >> 24));
	buf[1] = (uint8_t)(0xFF & (val >> 16));
	buf[2] = (uint8_t)(0xFF & (val >> 8));
	buf[3] = (uint8_t)(0xFF & (val >> 0));
}

//pack an int32_t value in big endian order
void pack(const int32_t val, std::span<uint8_t, std::dynamic_extent> buf) {
	pack(reinterpret_cast<const uint32_t&>(val), buf);
}

//pack an IEEE 754 single-precision float in big endian order
void pack(const float val, std::span<uint8_t, std::dynamic_extent> buf) {
	pack(reinterpret_cast<const uint32_t&>(val), buf);
}

//pack an ASCII string into bytes excluding any NULL termination
void pack(const std::string& text, std::span<uint8_t> buf) {
	assert(buf.size() >= text.size()); // or handle overflow more gracefully
	std::transform(text.begin(), text.end(), buf.begin(),
	               [](char c) { return static_cast<uint8_t>(c); });
	//need to include the last line to perform explicit type conversion from char to uint8_t
}

//unpack a uint32_t value in big endian order
uint32_t unpack_uint32(std::span<const uint8_t, std::dynamic_extent> buf) {
	uint32_t ret = buf[0] << 24;
	ret |= buf[1] << 16;
	ret |= buf[2] << 8;
	ret |= buf[3];
	return ret;
}

//unpack an int32_t value in big endian order
int32_t unpack_int32(std::span<const uint8_t, std::dynamic_extent> buf) {
	uint32_t ret = unpack_uint32(buf); //pull the bytes out into some 32-bit data structure
	return reinterpret_cast<uint32_t&>(ret); //reinterpret the bytes as an int32_t
}

//unpack an IEEE 754 single-precision float in big endian order
float unpack_float(std::span<const uint8_t, std::dynamic_extent> buf) {
	uint32_t ret = unpack_uint32(buf); //pull they bytes out into some 32-bit data structure
	return reinterpret_cast<float &>(ret); //reinterpret the bytes as an IEE 754 floating point number
}
