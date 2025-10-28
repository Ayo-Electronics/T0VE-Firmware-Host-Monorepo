/*
 * app_regmap_helpers.hpp
 *
 *  Created on: Jun 17, 2025
 *      Author: govis
 */

#pragma once

#include "app_proctypes.hpp"
#include "app_utils.hpp"

/*
 * `Regmap_Field` is a utility to help populate in particular bits into a buffer.
 * This is most useful when working with peripheral devices, i.e. SPI, I2C that interface via register reads/writes
 * Often, working with these devices involves setting certain bits in a buffer to control/read back from the devices.
 * This class aims to make the process of doing these bitwise twiddles easier/clearer in intent
 *
 * Some notes:
 *  - This class is somewhat dependent on how the processor lays out memory!
 *  	\--> the current implementation is sensitive to the endianness of the application processor
 *  	\--> this is due to a `memcpy` in the `read()`/`write()` functions that end up direct copying from memory
 *  	\--> set the endianness of the processor by adjusting the particular flag in `app_proctypes.hpp`
 * 	- `offset` refers to the position of the LEAST SIGNIFICANT BIT in the LSByte
 * 	- `base_byte` refers to the position of the START of the field, i.e. the LSByte.
 * 		\--> NOTE: endianness needs to be set correctly for this logic to work! 
 * 				   be extra careful with fields that are less than 8 bits but span multiple bytes
 */

//NOTE:
//

class Regmap_Field {
public:
	//just take in the particular buffer that we'll operate on
	//NON-owning! reference to buffer passed as a span
	//NOTE: no bounds checking on buffer because no logical failure mode--be careful!
	Regmap_Field(	const size_t _base_byte,
					const size_t _offset_bits,
					const size_t _field_width_bits,
					const bool _big_endian,
					std::span<uint8_t, std::dynamic_extent> _buffer):
		offset_bits(_offset_bits),
		field_width_bits(_field_width_bits),
		field_width_bytes(((_offset_bits + _field_width_bits + 7) / 8)),				// how many bytes we touch in our buffer (account for offset)
		mask((field_width_bits == 32) ? 0xFFFFFFFFu : ((1u << field_width_bits) - 1)),	// applied after shifting for read, before shifting for write
		big_endian(_big_endian),
		base_byte(_big_endian ?  _base_byte - (field_width_bytes - 1) : _base_byte),		//needs some special logic to handle--
		buffer(_buffer)
	{}

	//this is useful if there's some repeated structure in buffers and stuff like that
	//lets us use the same class but operate at different slots in the buffer
	inline void repoint(std::span<uint8_t, std::dynamic_extent> _buffer) {	buffer = _buffer; }

	//and have a size-aware endian swapping utility
	//we want to make sure the first byte we put into our buffer is in the LSB
	//as such, we need to shift the data after the 32-bit endian swap if necessary
	inline static uint32_t byte_swap(uint32_t v, size_t s) {
		if(s == 1) 	return v;	//swap_endian_32(v) >> 24 just gives us the same byte
		if(s == 2) 	return swap_endian_32(v) >> 16;
		if(s == 3) 	return swap_endian_32(v) >> 8;
		else		return swap_endian_32(v) >> 0;
	}

	//============================== READ/WRITE FUNCTIONS =============================

	//there's a bit of `value` sanity checking here to make sure it fits in the field appropriately
	//adds a little overhead but not that big of a deal most likely
	//NOTE: NOT THREAD SAFE!
	inline void write(uint32_t value) {
		//mask and shift our value appropriately
		value = (value & mask) << offset_bits;
		uint32_t clear_mask = ~(mask << offset_bits);

		//create our output temporary and copy into it
		uint32_t mod_field = 0;
		memcpy(&mod_field, buffer.data() + base_byte, field_width_bytes);

		//swap the endianness of the field if necessary
		//need to do this carefully/be aware of how big the particular field is
		if(big_endian != PROCESSOR_IS_BIG_ENDIAN) mod_field = byte_swap(mod_field, field_width_bytes);

		//reset and update our new values
		mod_field = (mod_field & clear_mask) | value;

		//re-swap endianness if necessary, and write back
		if(big_endian != PROCESSOR_IS_BIG_ENDIAN) mod_field = byte_swap(mod_field, field_width_bytes);
		memcpy(buffer.data() + base_byte, &mod_field, field_width_bytes);
	}

	//NOTE:NOT THREAD SAFE!
	inline uint32_t read() const {
		//create our output temporary and copy into it
		uint32_t out = 0;
		memcpy(&out, buffer.data() + base_byte, field_width_bytes);

		//swap the endianness of the field if necessary
		//need to do this carefully/be aware of how big the particular field is
		if(big_endian != PROCESSOR_IS_BIG_ENDIAN) out = byte_swap(out, field_width_bytes);

		//then mask, shift, and return
		return (out >> offset_bits) & mask;
	}

	//============================== OVERRIDES FOR EASY READING/WRITING =============================
	inline operator uint32_t() const { return read(); }		//reads the value of the field to a uint32_t
	inline void operator=(uint32_t value) { write(value); }	//write the value of the field as a uint32_t

private:
	const size_t offset_bits;
	const size_t field_width_bits;
	const size_t field_width_bytes;
	const uint32_t mask;
	const bool big_endian;
	const size_t base_byte;
	std::span<uint8_t, std::dynamic_extent> buffer;
};
