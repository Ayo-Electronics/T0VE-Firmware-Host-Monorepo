/*
 * app_regmap_helpers.hpp
 *
 *  Created on: Jun 17, 2025
 *      Author: govis
 */

#pragma once

#include <span>
#include "app_types.hpp"


//############################################ 0-8 bit register ##########################################
//NOTE: `byte_no` and `offset` refer to the position of the LEAST SIGNIFICANT BIT in the field

class Regmap_Field_8B {
public:

	//============================== CONSTRUCTOR =============================

	//just take in the particular buffer that we'll operate on
	//NON-owning! reference to buffer passed as a span
	//NOTE: no bounds checking on buffer because no logical failure mode--be careful!
	Regmap_Field_8B(const size_t _byte_no,
					const size_t _offset,
					const size_t _field_width,
					std::span<uint8_t, std::dynamic_extent> _buffer);

	//this is useful if there's some repeated structure in buffers and stuff like that
	//lets us use the same class but operate at different slots in the buffer
	void repoint(std::span<uint8_t, std::dynamic_extent> _buffer);

	//============================== READ/WRITE FUNCTIONS =============================

	//there's a bit of `value` sanity checking here to make sure it fits in the field appropriately
	//adds a little overhead but not that big of a deal most likely
	void write(uint8_t value);
	uint8_t read() const;

	//============================== OVERRIDES FOR EASY READING/WRITING =============================
	operator uint8_t() const; //read the value of the field to a uint8_t
	uint8_t operator=(uint8_t value); //write the value of the field as a uint8_t (return uint8_t for chaining)

private:
	const size_t byte_no;
	const size_t offset;
	const size_t field_width;
	std::span<uint8_t, std::dynamic_extent> buffer;
	const uint8_t mask;
};


//############################################ 9-16 bit register ##########################################
//NOTE: big endian, I'm using as the MSByte gets transmitted first, i.e. put in a lower memory address
//`base_byte_no` and `offset` refer to the position of the LEAST SIGNIFICANT BIT in the field

class Regmap_Field_16B {
public:

	//============================== CONSTRUCTOR =============================

	//just take in the particular buffer that we'll operate on
	//NON-owning! reference to buffer passed as a span
	//NOTE: no bounds checking on buffer due to lack of logical failure condition--BE CAREFUL!
	Regmap_Field_16B(	const size_t _base_byte_no,
						const size_t _offset,
						const size_t _field_width,
						const bool _big_endian,
						std::span<uint8_t, std::dynamic_extent> _buffer);

	//this is useful if there's some repeated structure in buffers and stuff like that
	//lets us use the same class but operate at different slots in the buffer
	void repoint(std::span<uint8_t, std::dynamic_extent> _buffer);

	//============================== READ/WRITE FUNCTIONS =============================

	//there's a bit of `value` sanity checking here to make sure it fits in the field appropriately
	//adds a little overhead but not that big of a deal most likely
	void write(uint16_t value);
	uint16_t read() const;

	//============================== OVERRIDES FOR EASY READING/WRITING =============================
	operator uint16_t() const;
	uint16_t operator=(uint16_t value);

private:
	Regmap_Field_8B low_byte;
	Regmap_Field_8B high_byte;
};

