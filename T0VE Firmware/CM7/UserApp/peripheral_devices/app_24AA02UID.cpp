/*
 * app_24AA02UID.cpp
 *
 *  Created on: Sep 4, 2025
 *      Author: govis
 */

#include "app_24AA02UID.hpp"

//constructor--just save the I2C bus
EEPROM_24AA02UID::EEPROM_24AA02UID(Aux_I2C& _bus):
	bus(_bus)
{}


void EEPROM_24AA02UID::init() {
	//initialze the bus
	bus.init();

	//check whether the device is present on the bus
	check_presence();

	//read the UID bytes (BLOCKING CALL)
	read_UID();

	//and finally read the contents (BLOCKING CALL)
	read_contents();
}


void EEPROM_24AA02UID::deinit() {
	//just deinit the bus itself
	bus.deinit();
}

/*
 * Checks whether the 24AA02UID is on the I2C bus.
 * BLOCK with this function, i.e. don't return until device detection routine concludes
 * Returns boolean value if present and also updates internal status variables
 *  \--> returns true if it is
 *  \--> false if it didn't ACK its address
 */
bool EEPROM_24AA02UID::check_presence() {
	//defer this to the I2C bus directly
	//automatically acquires the bus mutex and does its thing
	//blocking call
	device_present = bus.is_device_present(EEPROM_ADDR_7b);
	return device_present;
}

/*
 * Gets the device UID of the 24AA02UID as read from the initialization function
 *  \--> call `init()` again if you want a refreshed version of device ID
 */
uint32_t EEPROM_24AA02UID::get_UID() {
	//create a temporary where to copy the UID bytes
	std::array<uint8_t, UID_LENGTH_BYTES> _UID_bytes = UID_bytes;

	//then decode into the uint32_t
	//using the `unpack` routine in my utils class
	uint32_t UID = unpack_uint32(_UID_bytes);

	//and finally return our assembled UID
	return UID;
}

/*
 * Reports the contents of the memory, as read during the initialization sequence
 *	\--> call `init()` again if you want a refreshed version of the contents
 */
std::array<uint8_t, EEPROM_24AA02UID::MEMORY_SIZE_BYTES> EEPROM_24AA02UID::get_contents() {
	return eeprom_contents;
}

/*
 * Writes the array to the contents of the memory
 * 	\--> `get_contents()` will still only reflect the values read during `init()`
 * 	\--> call `init()` again if you want a refreshed version of the contents
 *	\--> returns whether the transmission was staged or not
 *	NOTE: `write_page` doesn't include the delay time needed to actually ensure the EEPROM is written!
 *	leaving for upstream applications to implement, EEPROM will NACK if not finished writing
 */
bool EEPROM_24AA02UID::write_page(	size_t start_address,
									std::span<uint8_t, PAGE_SIZE_BYTES> page_data,
									Callback_Function<> _write_error_cb)
{
	//if the device isn't present on the I2C bus
	if(!device_present) {
		_write_error_cb(); //report an error
		return true; //don't try to reschedule
	}

	//check if the start address is valid--otherwise just error out, don't try to reschedule
	//EEPROM will wrap around to beginning if address rolls to 0, likely not intended user behavior
	if((start_address + PAGE_SIZE_BYTES) > MEMORY_SIZE_BYTES) {
		_write_error_cb(); //report an error
		return true; //don't try to reschedule
	}

	//everything good, start by preparing a little TX buffer
	//gets copied to a DMA accessible buffer in the I2C class, can create this on the stack
	std::array<uint8_t, PAGE_SIZE_BYTES + 1> tx_buffer = {0};

	//first element should be the page address
	//the rest can be our page data
	tx_buffer[0] = start_address;
	std::copy(page_data.begin(), page_data.end(), tx_buffer.begin() + 1);

	//save our error callback right before we fire off this transmission
	write_error_cb = _write_error_cb;

	//configure and fire the transmission
	//will release the bus in the tx_complete callback
	auto result = bus.write(EEPROM_ADDR_7b, tx_buffer, BIND_CALLBACK(this, tx_complete));

	//and act appropriately depending on if the transmission went through
	if(result == Aux_I2C::I2C_STATUS::I2C_OK_READY) return true; //everything A-ok, transfer scheduled
	else if(result == Aux_I2C::I2C_STATUS::I2C_BUSY) return false; //peripheral was busy, transfer not scheduled
	else { //there was some kinda bus error
		write_error_cb(); 	//call the error callback
		return true;		//don't try to reschedule
	}
}


//=========================================== PRIVATE FUNCTIONS ==============================================
void EEPROM_24AA02UID::tx_complete() {
	//if we had a bus error, quickly run the provided transmit error handler
	if(bus.was_bus_success() == Aux_I2C::I2C_STATUS::I2C_ERROR)
		write_error_cb();
}

void EEPROM_24AA02UID::service_read_UID() {
	//check that the entire transmission went off without a hitch
	transfer_success = bus.was_bus_success() == Aux_I2C::I2C_STATUS::I2C_OK_READY;

	//if it did, then read the receive buffer into the device ID
	if(transfer_success) {
		//atomically copy over just the receive buffer
		Aux_I2C::I2C_STATUS st;
		UID_bytes.with([&](std::array<uint8_t, UID_LENGTH_BYTES>& _UID_bytes) {
			st = bus.retrieve(_UID_bytes);
		});

		//clear the transfer success flag if the decode wasn't successful for whatever reason
		if(st != Aux_I2C::I2C_STATUS::I2C_OK_READY) transfer_success = false;
	}

	//signal that the transmission is done
	transfer_complete_flag.signal();
}

void EEPROM_24AA02UID::read_UID() {
	//if we don't have a device on the bus, just return
	if(!device_present) return;

	//start by clearing the TX buffer, and set the first byte to the device ID register
	std::array<uint8_t, 1> tx_buffer = {UID_START_ADDRESS};

	//clear our signal flag, and run the transfer on the I2C BUS
	transfer_complete_flag.clear();
	Aux_I2C::I2C_STATUS status;
	do {
		status = bus.write_read(EEPROM_ADDR_7b,
								tx_buffer,
								UID_LENGTH_BYTES,
								BIND_CALLBACK(this, service_read_UID));
	} while(status == Aux_I2C::I2C_STATUS::I2C_BUSY); //stall until the transmission completes

	//and check if the transmission went through
	if(status != Aux_I2C::I2C_STATUS::I2C_OK_READY) {
		device_present = false;
		return;
	}

	//if it did, spin on our thread signal, waiting for the transfer to complete
	//timeout after a certain amount of time
	if(!transfer_complete_flag.wait(true, 5000)) {
		device_present = false;
		return;
	}

	//and check if the transmission completed successfully
	if(!transfer_success) {
		device_present = false;
		return;
	}
}

void EEPROM_24AA02UID::service_read_contents() {
	//check that the entire transmission went off without a hitch
	transfer_success = bus.was_bus_success() == Aux_I2C::I2C_STATUS::I2C_OK_READY;

	//if it did, then read the receive buffer into the device ID
	if(transfer_success) {
		//atomically copy over just the receive buffer
		Aux_I2C::I2C_STATUS st;
		eeprom_contents.with([&](std::array<uint8_t, MEMORY_SIZE_BYTES>& _eeprom_contents) {
			st = bus.retrieve(_eeprom_contents);
		});

		//clear the transfer success flag if the decode wasn't successful for whatever reason
		if(st != Aux_I2C::I2C_STATUS::I2C_OK_READY) transfer_success = false;
	}

	//signal that the transmission is done
	transfer_complete_flag.signal();
}

void EEPROM_24AA02UID::read_contents() {
	//if we don't have a device on the bus, just return
	if(!device_present) return;

	//start by clearing the TX buffer, and set the first byte to the device ID register
	std::array<uint8_t, 1> tx_buffer = {MEMORY_START_ADDRESS};

	//clear our signal flag, and run the transfer on the I2C BUS
	transfer_complete_flag.clear();
	Aux_I2C::I2C_STATUS status;
	do {
		status = bus.write_read(EEPROM_ADDR_7b,
								tx_buffer,
								MEMORY_SIZE_BYTES,
								BIND_CALLBACK(this, service_read_contents));
	} while(status == Aux_I2C::I2C_STATUS::I2C_BUSY); //stall until the transmission completes

	//and check if the transmission went through
	if(status != Aux_I2C::I2C_STATUS::I2C_OK_READY) {
		device_present = false;
		return;
	}

	//if it did, spin on our thread signal, waiting for the transfer to complete
	//timeout after a certain amount of time
	if(!transfer_complete_flag.wait(true, 5000)) {
		device_present = false;
		return;
	}

	//and check if the transmission completed successfully
	if(!transfer_success) {
		device_present = false;
		return;
	}
}
