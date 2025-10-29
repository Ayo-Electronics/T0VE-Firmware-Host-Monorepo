/*
 * app_hal_i2c.cpp
 *
 *  Created on: Jun 11, 2025
 *      Author: govis
 */

#include "app_hal_i2c.hpp"

//================================================= STATIC MEMBER DEFINITIONS ====================================================

// Define the main structure in normal RAM (no DMAMEM needed)
Aux_I2C::I2C_Hardware_Channel Aux_I2C::AUX_I2C_HARDWARE = {
	.i2c_handle = &hi2c4,
	.i2c_init_function = Callback_Function<>(MX_I2C4_Init),
	.i2c_deinit_function = Callback_Function<>( [](){HAL_I2C_DeInit(&hi2c4); }), //run the HAL de-init function
	.dma_init_function = Callback_Function<>(MX_BDMA_Init),
	.dma_deinit_function = Callback_Function<>(), //nothing for now, init function just enables clocking and sets interrupt priorities

	//to continue the transmission
	.continuing_transmission = false,
	.address_7b_continue = 0,
	.num_bytes_to_read_continue = 0,
	.rx_buffer_address = nullptr,

	//user buffer for post-read copies
	.user_rx_buffer = {},

	.transfer_complete_signal = nullptr,
	.transfer_error_signal = nullptr,
	.mutex = Mutex(),
};

//================================================= CONSTRUCTOR ====================================================
Aux_I2C::Aux_I2C(Aux_I2C::I2C_Hardware_Channel& _hardware):
		hardware(_hardware),

		//allocate space in the DMA memory pool for the the transmit and receive buffers
		i2c_tx_buffer(DMA_MEM_POOL::allocate_buffer<uint8_t, BUFFER_SIZES>()),
		i2c_rx_buffer(DMA_MEM_POOL::allocate_buffer<uint8_t, BUFFER_SIZES>())
{
	//point the RX buffer in the hardware to our RX buffer for our instance
	//useful for continued reads and copying into user rx buffer upon completion
	hardware.rx_buffer_address = i2c_rx_buffer.data();
}

//================================================= MEMBER FUNCTIONS ====================================================
void Aux_I2C::init() {
	if(is_init) return; //no need to repeat this process if we're initialized already

	//call the I2C and DMA initialization functions
	//ORDER IS VERY IMPORTANT! CALL DMA INITIALIZATION FIRST
	//this enables clocking to the DMA peripheral, which is required when I2C configures it
	hardware.dma_init_function();
	hardware.i2c_init_function();

	//register transfer complete and error callbacks
	HAL_I2C_RegisterCallback(hardware.i2c_handle, HAL_I2C_MASTER_TX_COMPLETE_CB_ID, AUX_I2C_TRANSFER_COMPLETE_cb);
	HAL_I2C_RegisterCallback(hardware.i2c_handle, HAL_I2C_MASTER_RX_COMPLETE_CB_ID, AUX_I2C_TRANSFER_COMPLETE_cb);
	HAL_I2C_RegisterCallback(hardware.i2c_handle, HAL_I2C_ERROR_CB_ID, AUX_I2C_BUS_ERR_cb);

	is_init = true; //assert the flag
}

void Aux_I2C::deinit() {
	//don't deinit if we're already de-init
	if(!is_init) return;

	//call the hardware de-init functions
	hardware.i2c_deinit_function();
	hardware.dma_deinit_function();

	//and clear the `is_init` flag
	is_init = false;
}

//detect whether an I2C device is present on the bus
bool Aux_I2C::is_device_present(uint8_t address_7b) {
	//create a flag that will be returned
	bool device_present;

	//perform the bus operation atomically
	hardware.mutex.WITH([&]() {
		//clear the transfer complete/error signals in case we error out during the process
		hardware.transfer_complete_signal = nullptr;
		hardware.transfer_error_signal = nullptr;
		hardware.user_rx_buffer = {};

		//according to function handle, we need to shift the address left
		//only try for a little bit (20ms)
		device_present = HAL_I2C_IsDeviceReady(hardware.i2c_handle, address_7b << 1, 1, 20) == HAL_OK;
	});

	//return whether the device was present
	return device_present;
}

//Write to I2C Device
Aux_I2C::I2C_STATUS Aux_I2C::write(	uint8_t address_7b, std::span<uint8_t, std::dynamic_extent> bytes_to_transmit,
									Thread_Signal* tx_complete_signal, Thread_Signal* tx_error_signal)
{
	//start by checking if the bus is available--if not, return BUSY
	//if it's free claim the mutex
	if(!hardware.mutex.TRY_LOCK()) return I2C_STATUS::I2C_BUSY;

	//then check to see if the peripheral isn't ready for another bus operation
	if(hardware.i2c_handle->State != HAL_I2C_STATE_READY) {
		hardware.mutex.UNLOCK();
		return I2C_STATUS::I2C_BUSY;
	}

	//if we can't fit the bytes into the I2C tx buffer, error out
	if(bytes_to_transmit.size() > i2c_tx_buffer.size()) {
		hardware.mutex.UNLOCK();
		return I2C_STATUS::I2C_ERROR;
	}

	//everything is good--copy the bytes into the transmit buffer
	//reset our "user_bytes_receive" span to empty to tell the transfer complete callback not to copy from rx buffer
	//attach the transmission complete callback, set the "all good" flag, and fire off the DMA transmit
	std::copy(bytes_to_transmit.begin(), bytes_to_transmit.end(), i2c_tx_buffer.begin());
	hardware.user_rx_buffer = {};
	hardware.transfer_complete_signal = tx_complete_signal;
	hardware.transfer_error_signal = tx_error_signal;
	auto success = HAL_I2C_Master_Transmit_DMA(hardware.i2c_handle, address_7b << 1, i2c_tx_buffer.data(), bytes_to_transmit.size());

	//check we successfully staged our transfer
	if(success != HAL_OK) {
		hardware.mutex.UNLOCK();
		return I2C_STATUS::I2C_ERROR;
	}

	//if we have, return okay, CONTINUE HOLDING THE MUTEX UNTIL THE TRANSFER COMPLETES
	return I2C_STATUS::I2C_OK_READY;
}

//Read from I2C Device
//read into our class's receive buffer--is in the correct memory region
Aux_I2C::I2C_STATUS Aux_I2C::read(	uint8_t address_7b, std::span<uint8_t, std::dynamic_extent> bytes_to_receive,
									Thread_Signal* rx_complete_signal, Thread_Signal* rx_error_signal)
{
	//start by checking if the bus is available--if not, return BUSY
	//if it's free claim the mutex
	if(!hardware.mutex.TRY_LOCK()) return I2C_STATUS::I2C_BUSY;

	//then check to see if the peripheral isn't ready for another bus operation
	if(hardware.i2c_handle->State != HAL_I2C_STATE_READY) {
		hardware.mutex.UNLOCK();
		return I2C_STATUS::I2C_BUSY;
	}

	//if we can't fit the bytes into the I2C rx buffer, error out
	if(bytes_to_receive.size() > i2c_rx_buffer.size()) {
		hardware.mutex.UNLOCK();
		return I2C_STATUS::I2C_ERROR;
	}

	//everything is good--attach the thread signals along with the user destination
	//bytes will be copied into the buffer upon transmission success
	hardware.user_rx_buffer = bytes_to_receive;
	hardware.transfer_complete_signal = rx_complete_signal;
	hardware.transfer_error_signal = rx_error_signal;
	auto success = HAL_I2C_Master_Receive_DMA(hardware.i2c_handle, address_7b << 1, i2c_rx_buffer.data(), bytes_to_receive.size());

	//check we successfully staged our transfer
	if(success != HAL_OK) {
		hardware.mutex.UNLOCK();
		return I2C_STATUS::I2C_ERROR;
	}

	//if we have, return okay, CONTINUE HOLDING THE MUTEX UNTIL THE TRANSFER COMPLETES
	return I2C_STATUS::I2C_OK_READY;
}

//write data to the address, then read data from the address using repeated start
Aux_I2C::I2C_STATUS Aux_I2C::write_read(	uint8_t address_7b,
											std::span<uint8_t, std::dynamic_extent> bytes_to_transmit,
											std::span<uint8_t, std::dynamic_extent> bytes_to_receive,
											Thread_Signal* _transfer_complete_signal, Thread_Signal* _transfer_error_signal)
{
	//start by checking if the bus is available--if not, return BUSY
	//if it's free claim the mutex
	if(!hardware.mutex.TRY_LOCK()) return I2C_STATUS::I2C_BUSY;

	//then check to see if the peripheral isn't ready for another bus operation
	if(hardware.i2c_handle->State != HAL_I2C_STATE_READY) {
		hardware.mutex.UNLOCK();
		return I2C_STATUS::I2C_BUSY;
	}

	//if we can't fit the bytes into the I2C tx buffer, error out
	if(bytes_to_transmit.size() > i2c_tx_buffer.size()) {
		hardware.mutex.UNLOCK();
		return I2C_STATUS::I2C_ERROR;
	}

	//if we can't fit the bytes into the I2C rx buffer, error out
	if(bytes_to_receive.size() > i2c_rx_buffer.size()) {
		hardware.mutex.UNLOCK();
		return I2C_STATUS::I2C_ERROR;
	}

	//everything is good--save the number of bytes we want to read after this function exits
	//and assert a flag in the hardware struct saying we'd like to continue this transmission
	//i.e. hold onto the mutex, schedule a read, when the write finishes
	hardware.num_bytes_to_read_continue = bytes_to_receive.size();
	hardware.address_7b_continue = address_7b;
	hardware.continuing_transmission = true;
	hardware.user_rx_buffer = bytes_to_receive;

	//then copy the bytes into the transmit buffer
	//attach the thread signals and fire off the DMA transmit
	std::copy(bytes_to_transmit.begin(), bytes_to_transmit.end(), i2c_tx_buffer.begin());
	hardware.user_rx_buffer = bytes_to_receive;
	hardware.transfer_complete_signal = _transfer_complete_signal;
	hardware.transfer_error_signal = _transfer_error_signal;
	auto success = HAL_I2C_Master_Transmit_DMA(hardware.i2c_handle, address_7b << 1, i2c_tx_buffer.data(), bytes_to_transmit.size());

	//check we successfully staged our transfer
	if(success != HAL_OK) {
		hardware.mutex.UNLOCK();
		return I2C_STATUS::I2C_ERROR;
	}

	//if we have, return okay, CONTINUE HOLDING THE MUTEX UNTIL THE TRANSFER COMPLETES
	return I2C_STATUS::I2C_OK_READY;
}

//======================================== PROCESSOR ISR CALLBACKS ======================================
//this function gets called when both when transmission and reception finishes
//since only one can happen at a time on the bus, just a single function should be fine
//having a single function simplifies event routing during an error condition (don't have to externally maintain state of transmitting vs receiving)
void AUX_I2C_TRANSFER_COMPLETE_cb(I2C_HandleTypeDef* i2c_handle) {
	//a little helper so we don't have to type stuff out as much
	auto& hw = Aux_I2C::AUX_I2C_HARDWARE;

	//forward the call to the upstream module that connected its callback function
	//this conditional check up front is a bit redundant but is an extra sanity check this function is getting called for the right reason
	if(i2c_handle == hw.i2c_handle) {
		//if we'd like to continue the transmission, fire off the `read()` half
		if(hw.continuing_transmission) {
			//don't try to continue this transmission after the `read()` half
			hw.continuing_transmission = false;

			//stage the read half
			//doing this in the ISR context should be safe, since the I2C state machine has updated
			auto success = HAL_I2C_Master_Receive_DMA(	hw.i2c_handle,
														hw.address_7b_continue << 1,
														hw.rx_buffer_address,
														hw.num_bytes_to_read_continue);

			//check we successfully staged our transfer, we're done here if so
			if(success == HAL_OK) return;

			//and if we didn't, release the mutex and signal error
			//do the mutex release first in case a transmission is retried on failure
			hw.mutex.UNLOCK();
			if(hw.transfer_error_signal) hw.transfer_error_signal->signal();
		}

		//we're not continuing the transmission, i.e. transfer finished normally
		else {
			//copy bytes from the RX buffer if we wanted to receive some bytes
			if(hw.user_rx_buffer.size())
				memcpy(hw.user_rx_buffer.data(), hw.rx_buffer_address, hw.user_rx_buffer.size_bytes());

			//release the mutex and signal success
			//do the mutex release first in case we fire another transmission on success (prevents lock contention)
			hw.mutex.UNLOCK();
			if(hw.transfer_complete_signal) hw.transfer_complete_signal->signal();
		}
	}
}

void AUX_I2C_BUS_ERR_cb(I2C_HandleTypeDef* i2c_handle) {
	//a little helper so we don't have to type stuff out as much
	auto& hw = Aux_I2C::AUX_I2C_HARDWARE;

	//attempt to recover the bus
	//then unlock the peripheral and signal an error
	if(i2c_handle == hw.i2c_handle) {
		//clear the transmission continue flag if it had been set
		hw.continuing_transmission = false;

		//de-initialize and re-initialize the peripherals--attempt to "reset" bus errors
		hw.i2c_deinit_function();
		hw.dma_deinit_function();
		hw.dma_init_function();
		hw.i2c_init_function();

		//unlock the peripheral and signal an error
		hw.mutex.UNLOCK(); //release the mutex after servicing the user callback
		if(hw.transfer_error_signal) hw.transfer_error_signal->signal();
	}
}
