/*
 * app_hispeed_analog.hpp
 *
 *  Created on: Aug 25, 2025
 *      Author: govis
 */

#pragma once

#include "app_types.hpp"
#include "app_hal_gpio.hpp"
#include "app_hal_spi.hpp"
#include "app_hal_pin_mapping.hpp"

class Hispeed_Analog {
public:

	void init(); //likely not gonna do anything here, putting here as a hook

	void activate(); 	//initializes the I/O lines/SPI bus for regular operation (for when analog power is enabled)
	void deactivate();	//puts the SPI I/O lines in Hi-Z mode (for when power is disabled)

	void arm(); 	//puts the chip-select I/O in alternate mode for timer-based control
	void disarm();	//puts the chip-select I/O in regular GPIO mode for register-based control

	//heavily optimized routine for high speed read/writes
	//directly write to the SPI bus transmit/receive registers
	#pragma GCC push_options
	#pragma GCC optimize ("Ofast,unroll-loops,inline-functions")

	__attribute__((always_inline)) inline uint32_t READY_READ()			{ return bus.ready_read(); }
	__attribute__((always_inline)) inline uint16_t RAW_READ() 			{ return bus.read(); }
	__attribute__((always_inline)) inline uint32_t READY_WRITE() 		{ return bus.ready_write(); }
	__attribute__((always_inline)) inline void RAW_WRITE(uint16_t val) 	{ bus.write(val); }
	__attribute__((always_inline)) inline uint32_t READ_DAC_CS()		{ return cs_dac.read(); } //using these in the high-speed loop
	__attribute__((always_inline)) inline uint32_t READ_ADC_CS()		{ return cs_adc.read(); }

	#pragma GCC pop_options

	uint16_t read(); //use when we just want to read from the ADC --> will start a conversion then shift out the result
	void write(uint16_t dac_val); //use when we just want to write to the DAC
	uint16_t transfer(uint16_t dac_val); //use when we want to transfer both with ADC/DAC --> calls write then read sequentially

	//constructor, deleting copy constructor, assignment operator
	Hispeed_Analog(	HiSpeed_SPI::SPI_Hardware_Channel& _bus,
					GPIO_Alternate::GPIO_Alternate_Hardware_Pin _cs_dac,
					GPIO_Alternate::GPIO_Alternate_Hardware_Pin _cs_adc	);
	Hispeed_Analog(const Hispeed_Analog& other) = delete;
	void operator=(const Hispeed_Analog& other) = delete;


private:
	//own a high-speed SPI bus, along with chip-select lines
	HiSpeed_SPI bus;
	GPIO_Alternate cs_dac;
	GPIO_Alternate cs_adc;
};
