/*
 * app_hal_spi.hpp
 *
 *  Created on: Jun 26, 2025
 *      Author: govis
 * 
 * NOTE: Currently, this class has been outfitted to test the SPI interface. In the real application,
 * SPI transactions will be taken care of by the DMA controller automatically via a triggering system. 
 * Future versions of this API will include an `arm` and `disarm` function to enable automatic transmissions
 * Chip select for the SPI interface will normally be controlled via triggered timer interfaces. 
 * As such, `arm` will relinquish GPIO control (resetting it to alternate function or whatever), and
 * `disarm` will configure the GPIO pin as a firmware-controlled output. 
 * 
 * These API functions will be added in the future when we work on the full firmware interface
 */

#pragma once

#include <app_proctypes.hpp>
#include "app_utils.hpp"
#include "app_hal_gpio.hpp"

#include "spi.h"

class HiSpeed_SPI {

public:
    //================================ TYPEDEFS ================================

	struct SPI_Hardware_Channel {
		SPI_HandleTypeDef* const spi_handle;
		//these are the functions that will be called to initialize SPI peripherals
		const Callback_Function<> spi_init_function;
		const Callback_Function<> spi_deinit_function;
		volatile uint32_t* const TXDR_addr;
		volatile uint32_t* const RXDR_addr;
		volatile uint32_t* const SR_addr;
		volatile uint32_t* const CR1_addr;
	};

    // We have 4 SPI channels on the board
	static SPI_Hardware_Channel SPI_CHANNEL_0;
	static SPI_Hardware_Channel SPI_CHANNEL_1;
    static SPI_Hardware_Channel SPI_CHANNEL_2;
    static SPI_Hardware_Channel SPI_CHANNEL_3;


	//some status enums to provide information back to calling threads
	enum class SPI_STATUS {
		SPI_OK_READY,
		SPI_BUSY,
		SPI_ERROR
	};

	//================================= INSTANCE METHODS ===================================
	void init(); //calls the HAL function that initializes the SPI bus and pins
	void deinit(); //purges FIFO, deinitializes the peripheral, tri-states bus

	void purge(); //function to purge the TX/RX FIFOs

	//have some high-speed functions for transfer related accesses
	#pragma GCC push_options
	#pragma GCC optimize ("Ofast,unroll-loops,inline-functions")

	__attribute__((always_inline)) inline uint32_t ready_write()	{ return ((uint32_t)SPI_SR & SPI_SR_TXP); }
	__attribute__((always_inline)) inline void write(uint16_t val) 	{ SPI_TXDR = val; }
	__attribute__((always_inline)) inline uint32_t ready_read()		{ return ((uint32_t)SPI_SR & SPI_SR_RXP); }
	__attribute__((always_inline)) inline uint16_t read() 			{ return SPI_RXDR; }
	__attribute__((always_inline)) inline uint16_t is_init() 		{ return ((uint32_t)SPI_CR1 & SPI_CR1_SPE); }

	#pragma GCC pop_options


	//for low-speed SPI transfers, use this function
	//performs everything atomically; waits until transfer completion
	//returns the byte read from the bus
	//this version of SPI transfer controls the chip select GPIO pins
	//control the chip select lines depending on whether we want to actually transfer from the DAC or ADC
	uint16_t transfer(uint16_t write_data);

	//========================= CONSTRUCTORS, DESTRUCTORS, OVERLOADS =========================
    //constructor will just take a reference to the hardware pin
	HiSpeed_SPI(SPI_Hardware_Channel& _hardware);

	//delete assignment operator and copy constructor
	//in order to prevent hardware conflicts
	HiSpeed_SPI(HiSpeed_SPI const& other) = delete;
	void operator=(HiSpeed_SPI const& other) = delete;

private:
	//save the hardware structure that the user passes in
	SPI_Hardware_Channel& hardware;

	//have some key registers that hold SPI information
	Register<uint16_t> SPI_TXDR; //where to dump outgoing data
	Register<uint16_t> SPI_RXDR; //where to get incoming data
	Register<uint32_t> SPI_SR;   //where to check busy information
	Register<uint32_t> SPI_CR1;  //where to init information
	static const uint32_t SPI_SR_BUSY_MASK = SPI_SR_EOT; //end-of-transfer bit in SPI status register
};
