/*
 * app_hal_i2c.hpp
 *
 *  Created on: Jun 11, 2025
 *      Author: govis
 */

#pragma once

#include "app_proctypes.hpp"
#include <array> //for array, span

#include "app_utils.hpp" //for callback function
#include "app_dma_mem_pool.hpp" //to put transmit and receive buffers into DMA memory regions
#include "app_threading.hpp" //for basic mutexing system, thread signals
#include "app_scheduler.hpp" //to defer calls from non-ISR context

#include "i2c.h" //HAL I2C Stuff
#include "bdma.h" //for DMA Stuff

class Aux_I2C {
public:
	//================================ TYPEDEFS ================================
	static const size_t BUFFER_SIZES = 128;

	//NOTE: need to place these in a section that is accessible by DMA
	//use the `DMAMEM` macro to place these in the appropriate section
	struct I2C_Hardware_Channel {
		I2C_HandleTypeDef* const i2c_handle;
		//these are the functions that will be called to initialize the I2C and DMA peripherals
		const Callback_Function<> i2c_init_function;
		const Callback_Function<> i2c_deinit_function;
		const Callback_Function<> dma_init_function;
		const Callback_Function<> dma_deinit_function;

		//for write-read sequences, save this information
		bool continuing_transmission;
		uint8_t address_7b_continue = 0; //save this for when we need to restart communication autonomously
		size_t num_bytes_to_read_continue = 0; //how many bytes we want to read after we continue the transmission
		uint8_t* rx_buffer_address;

		//for reads, copy data directly into the user receive buffer after transfer success
		std::span<uint8_t, std::dynamic_extent> user_rx_buffer;

		//and some threading primitives for access control and notification
		Thread_Signal* transfer_complete_signal;
		Thread_Signal* transfer_error_signal;
		Mutex mutex;
	};

	static I2C_Hardware_Channel AUX_I2C_HARDWARE;

	//some status enums to provide information back to calling threads
	enum class I2C_STATUS {
		I2C_OK_READY,
		I2C_BUSY,
		I2C_ERROR
	};

	//================================= INSTANCE METHODS ===================================
	void init(); //just calls HAL function that configures the GPIO pin in its default (cube) setting
	void deinit(); //TBD

	//function to detect whether a certain device is on the I2C bus
	//will return `true` if an `ACK` bit is received for the address
	//will automatically claim/release the bus to perform the operation, so don't need to worry here
	//BLOCKING call--waits for a complete bus transaction
	bool is_device_present(uint8_t address_7b);

	//###### functions to read and write to the bus ######
	//this is just an extra step to ensure no resource usage conflicts
	//`write` will copy the bytes to a local buffer, ensuring buffer does not change during transmit
	//asserts the appropriate signal flag upon transfer complete or transfer error
	I2C_STATUS write(	uint8_t address_7b, std::span<uint8_t, std::dynamic_extent> bytes_to_transmit,
						Thread_Signal* tx_complete_signal = nullptr, Thread_Signal* tx_error_signal = nullptr);

	//`read` will read a determined number of bytes into the local RX buffer via DMA
	//the bytes can then recalled using the `retrieve()` function and passing in a span of appropriate size
	//the appropriate signal will be raised upon transfer complete or transfer error
	I2C_STATUS read(	uint8_t address_7b, std::span<uint8_t, std::dynamic_extent> bytes_to_receive,
						Thread_Signal* rx_complete_signal = nullptr, Thread_Signal* rx_error_signal = nullptr);

	//`write_read` will write the given number of bytes on the bus
	//then ideally perform a `repeated_start` (but more likely a stop, then start with the mutex still held)
	//then read the prescribed number of bytes on the bus
	//this is a non-blocking transfer, and will call the callback function when complete
	//will return `I2C_STATUS_OK_READY` if transfer staged successfully, and something else if not
	I2C_STATUS write_read(	uint8_t address_7b,
							std::span<uint8_t, std::dynamic_extent> bytes_to_transmit,
							std::span<uint8_t, std::dynamic_extent> bytes_to_receive,
							Thread_Signal* _transfer_complete_signal = nullptr, Thread_Signal* _transfer_error_signal = nullptr);

	//========================= CONSTRUCTORS, DESTRUCTORS, OVERLOADS =========================
	Aux_I2C(I2C_Hardware_Channel& _hardware);

	//delete assignment operator and copy constructor
	//in order to prevent hardware conflicts
	Aux_I2C(Aux_I2C const& other) = delete;
	void operator=(Aux_I2C const& other) = delete;

private:
	//a little flag that says if we've already been initialized
	bool is_init = false;

	//save the hardware structure that the user passes in
	I2C_Hardware_Channel& hardware;

	//maintain a transmit buffer in which to copy bytes
	//statically initializing this to a fixed size, change this size if needed
	std::span<uint8_t, std::dynamic_extent> i2c_tx_buffer;
	std::span<uint8_t, std::dynamic_extent> i2c_rx_buffer;
};

//======================================== PROCESSOR ISR CALLBACKS PROTOTYPES ======================================

void AUX_I2C_TRANSFER_COMPLETE_cb(I2C_HandleTypeDef* i2c_handle);
void AUX_I2C_BUS_ERR_cb(I2C_HandleTypeDef* i2c_handle);

