/*
 * app_24AA02UID.hpp
 *
 *  Created on: Sep 4, 2025
 *      Author: govis
 */

#pragma once

#include "app_proctypes.hpp"
#include "app_hal_i2c.hpp"
#include "app_regmap_helpers.hpp"

class EEPROM_24AA02UID {
public:
	//========================== CONSTANTS AND TYPEDEFS ========================
	//size of the memory in bytes
	static constexpr size_t MEMORY_SIZE_BYTES = 128;
	static constexpr size_t PAGE_SIZE_BYTES = 8;
	static constexpr size_t NUM_PAGES = MEMORY_SIZE_BYTES/PAGE_SIZE_BYTES;

	//and have a very conservative write cycle time
	//if we want to schedule multiple page-writes, wait this long
	static constexpr uint32_t WRITE_CYCLE_TIME_MS = 10;

	//========================== PUBLIC FUNCTIONS ===========================
	EEPROM_24AA02UID(Aux_I2C& _bus);

	//delete copy constructor and assignment operator
	EEPROM_24AA02UID(const EEPROM_24AA02UID& other) = delete;
	void operator=(const EEPROM_24AA02UID& other) = delete;

	/*
	 * Initialize everything
	 *  \--> initialize the bus
	 *  \--> checks whether device is on the bus
	 *  \--> reads the UID bytes
	 *  \--> reads the entire contents of the user-portion of the memory
	 */
	void init();

	/*
	 * Deinit
	 * 	\--> just de-init the I2C bus; puts everything in tri-state
	 * 	\--> useful when the bus is going down due to power off command
	 */
	void deinit();

	/*
	 * Checks whether the 24AA02UID is on the I2C bus.
	 * BLOCK with this function, i.e. don't return until device detection routine concludes
	 * Returns boolean value if present and also updates internal status variables
	 *  \--> returns true if it is
	 *  \--> false if it didn't ACK its address
	 */
	bool check_presence();

	/*
	 * Gets the device UID of the 24AA02UID as read from the initialization function
	 *  \--> call `init()` again if you want a refreshed version of device ID
	 * NOTE: only call this after init, when I2C transactions are finished!
	 */
	uint32_t get_UID();

	/*
	 * Reports the contents of the memory, as read during the initialization sequence
	 *	\--> call `init()` again if you want a refreshed version of the contents
	 * NOTE: only call this after init, when I2C transactions are finished!
	 */
	std::array<uint8_t, MEMORY_SIZE_BYTES> get_contents();

	/*
	 * Writes the array to the contents of the memory
	 * 	\--> `get_contents()` will still only reflect the values read during `init()`
	 * 	\--> call `init()` again if you want a refreshed version of the contents
	 *	\--> returns whether the transmission was staged or not
	 */
	bool write_page(size_t start_address, std::span<uint8_t, PAGE_SIZE_BYTES> page_data, Thread_Signal* write_error_signal);

private:
	Aux_I2C& bus; //reference to the i2c bus hardware
	static const uint8_t EEPROM_ADDR_7b = 0b1010000;	//I2C address of the EEPROM

	//have a function that reads the UID bytes from the eeprom
	//as well as a place to keep those bytes
	//thread/ISR safe wrapper to ensure atomicity
	void read_UID();
	static const size_t UID_LENGTH_BYTES = 4;
	static const uint8_t UID_START_ADDRESS = 0xFC;
	std::array<uint8_t, UID_LENGTH_BYTES> UID_bytes;	//dump the UID bytes into here, NOT THREAD SAFE

	//and have a function that reads the entire contents of the eeprom
	//as well as a place to keep those bytes
	//thread/ISR safe wrapper to ensure atomicity
	void read_contents();
	static const size_t MEMORY_START_ADDRESS = 0;
	std::array<uint8_t, MEMORY_SIZE_BYTES> eeprom_contents;	//dump EEPROM contents here, NOT THREAD SAFE

	//flag that holds whether the device is present on the bus
	//if the device is absent, functions will just return to reduce bus overhead
	bool device_present;

	//and have a little thread signal that signals the non-ISR thread that our write has completed
	//have both a write complete and write error flag
	PERSISTENT((Thread_Signal), internal_transfer_complete);
	PERSISTENT((Thread_Signal), internal_transfer_error);

};
