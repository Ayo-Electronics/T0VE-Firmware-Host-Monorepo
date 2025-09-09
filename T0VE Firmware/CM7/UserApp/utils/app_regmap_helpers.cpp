/*
 * app_regmap_helpers.cpp
 *
 *  Created on: Jun 17, 2025
 *      Author: govis
 */

#include "app_regmap_helpers.hpp"

//############################################ 0-8 bit register ##########################################
//NOTE: `byte_no` and `offset` refer to the position of the LEAST SIGNIFICANT BIT in the field

Regmap_Field_8B::Regmap_Field_8B(const size_t _byte_no,
                                 const size_t _offset,
                                 const size_t _field_width,
                                 std::span<uint8_t, std::dynamic_extent> _buffer):
    //save everything to member variables
	byte_no(_byte_no),
    offset(_offset),
    field_width(_field_width),
    buffer(_buffer),
    mask((1 << _field_width) - 1)
{
	//assert((_offset + _field_width) <= 8, "offset and/or width too large"); //a little sanity check when we can 
}

void Regmap_Field_8B::repoint(std::span<uint8_t, std::dynamic_extent> _buffer) {
	buffer = _buffer;
}

void Regmap_Field_8B::write(uint8_t value) {
	//clear the field and then write the new value
	buffer[byte_no] = (buffer[byte_no] & ~(mask << offset)) | (value & mask) << offset;
}

uint8_t Regmap_Field_8B::read() const {	
	//just read the field and return it
	return (buffer[byte_no] >> offset) & mask;
}

Regmap_Field_8B::operator uint8_t() const { return read(); } //just return the value of the field

//just write the value of the field as a uint8_t
uint8_t Regmap_Field_8B::operator=(uint8_t value) {
	write(value);
	return value;
}

//############################################ 9-16 bit register ##########################################
//NOTE: big endian, I'm using as the MSByte gets transmitted first, i.e. put in a lower memory address
//`base_byte_no` and `offset` refer to the position of the LEAST SIGNIFICANT BIT in the field

Regmap_Field_16B::Regmap_Field_16B(const size_t _base_byte_no,
                                   const size_t _offset,
                                   const size_t _field_width,
                                   const bool _big_endian,
                                   std::span<uint8_t, std::dynamic_extent> _buffer):
    low_byte(_base_byte_no, _offset, 8-_offset, _buffer), //low byte contains the first couple of bits
    high_byte(_base_byte_no + (_big_endian ? -1 : 1), 0, _field_width + _offset - 8, _buffer) //ASSUMING no high-byte offset 
{
	//assert((_offset + _field_width) > 8, "field doesn't span across two bytes -- use 8B field instead");
	//assert((_offset + _field_width) <= 16, "offset and/or width too large");
}

void Regmap_Field_16B::repoint(std::span<uint8_t, std::dynamic_extent> _buffer) {
	low_byte.repoint(_buffer);
	high_byte.repoint(_buffer);
}

void Regmap_Field_16B::write(uint16_t value) {
	low_byte = value & 0xFF; //write the low byte
	high_byte = value >> 8; //write the high byte
}

//some bitwise stuff for reading from the 16 bit value
uint16_t Regmap_Field_16B::read() const {
	uint16_t output = 0;
	output = (uint16_t)high_byte.read();
	output = output << 8;
	output |= (uint16_t)low_byte.read();
	return output;
}

//just write the value of the field as a uint16_t
//return uint16_t for chaining
uint16_t Regmap_Field_16B::operator=(uint16_t value) {
	write(value);
	return value;
}

//cast to 16-bits equates to a read of the register
Regmap_Field_16B::operator uint16_t() const { return read(); }
