/*
 * app_hal_spi.cpp
 *
 *  Created on: Jun 26, 2025
 *      Author: govis
 */

#include "app_hal_spi.hpp"

//============================== STATIC MEMBER DEFINITIONS ==============================

// NOTE: These need to be defined outside the class
HiSpeed_SPI::SPI_Hardware_Channel HiSpeed_SPI::SPI_CHANNEL_0 = {
    .spi_handle = &hspi6,
    .spi_init_function = Callback_Function<>(MX_SPI6_Init),
    .spi_deinit_function = Callback_Function<>([](){ HAL_SPI_DeInit(&hspi6); }), //use full HAL de-init
	.TXDR_addr = &SPI6->TXDR,
	.RXDR_addr = &SPI6->RXDR,
	.SR_addr = &SPI6->SR,
	.CR1_addr = &SPI6->CR1
};

HiSpeed_SPI::SPI_Hardware_Channel HiSpeed_SPI::SPI_CHANNEL_1 = {
    .spi_handle = &hspi2,
    .spi_init_function = Callback_Function<>(MX_SPI2_Init),
    .spi_deinit_function = Callback_Function<>([](){ HAL_SPI_DeInit(&hspi2); }),
	.TXDR_addr = &SPI2->TXDR,
	.RXDR_addr = &SPI2->RXDR,
	.SR_addr = &SPI2->SR,
	.CR1_addr = &SPI2->CR1
};

HiSpeed_SPI::SPI_Hardware_Channel HiSpeed_SPI::SPI_CHANNEL_2 = {
    .spi_handle = &hspi1,
    .spi_init_function = Callback_Function<>(MX_SPI1_Init),
    .spi_deinit_function = Callback_Function<>([](){ HAL_SPI_DeInit(&hspi1); }),
	.TXDR_addr = &SPI1->TXDR,
	.RXDR_addr = &SPI1->RXDR,
	.SR_addr = &SPI1->SR,
	.CR1_addr = &SPI1->CR1
};

HiSpeed_SPI::SPI_Hardware_Channel HiSpeed_SPI::SPI_CHANNEL_3 = {
    .spi_handle = &hspi5,
    .spi_init_function = Callback_Function<>(MX_SPI5_Init),
    .spi_deinit_function = Callback_Function<>([](){ HAL_SPI_DeInit(&hspi5); }),
	.TXDR_addr = &SPI5->TXDR,
	.RXDR_addr = &SPI5->RXDR,
	.SR_addr = &SPI5->SR,
	.CR1_addr = &SPI5->CR1
};

//============================== CONSTRUCTOR/DESTRUCTOR ================================

HiSpeed_SPI::HiSpeed_SPI(SPI_Hardware_Channel& _hardware)
    : hardware(_hardware),
	  SPI_TXDR(reinterpret_cast<volatile uint16_t*>(hardware.TXDR_addr)), //ensure we write in 16-bit frames
	  SPI_RXDR(reinterpret_cast<volatile uint16_t*>(hardware.RXDR_addr)), //ensure we read in 16-bit frames
	  SPI_SR(hardware.SR_addr),
	  SPI_CR1(hardware.CR1_addr)
{}

//============================== INSTANCE METHODS ======================================

void HiSpeed_SPI::init()
{
	//initialize the hardware peripheral
	//I/O initialization is taken care of here
	hardware.spi_init_function();

	//and for our special use case, we need to put the peripheral in streaming mode
	//set our TSIZE to zero --> immediately transfers a packet when data is placed in the TX FIFO
	hardware.spi_handle->Instance->CR2 = 0;

	//then enable the SPI peripheral
	hardware.spi_handle->Instance->CR1 |= SPI_CR1_SPE;

	//and set the CSTART bit to enable the transmitter
	hardware.spi_handle->Instance->CR1 |= SPI_CR1_CSTART;

	//clear any stale RX data before first use
	purge();
}

void HiSpeed_SPI::deinit() {
	//ensure any ongoing transfer completes and RX is drained before shutdown
	purge();

	//clear CSTART bit before we de-init --> needs it to be reset before we initialize again
	hardware.spi_handle->Instance->CR1 &= ~SPI_CR1_CSTART;

	//disable the SPI peripheral
	hardware.spi_handle->Instance->CR1 &= ~SPI_CR1_SPE;

	//run the peripheral de-init function (full HAL reset/deinit)
	//I/O de-initialization is also taken care of here
	hardware.spi_deinit_function();
}

void HiSpeed_SPI::purge() {
	// Drain RX FIFO
	//just reads it into the ether
    while (ready_read()) (void)read();

    //And wiat until the TX FIFO is empty
    //simply waits for stuff to be shifted out on the bus
    while (!(hardware.spi_handle->Instance->SR & SPI_SR_TXC));
}


//run a blocking transfer using the SPI bus
//chip select is handled externally to this class!
uint16_t HiSpeed_SPI::transfer(uint16_t write_data) {
	//wait until we're ready to accept another half-word
	while(!ready_write());

	//write the data out using the low-level write functions
	write(write_data);

	//spin until we're ready to read the half-word
	while(!ready_read());

	//grab the data we read from the bus (or whatever is in RXDR)
	return(read());
}



