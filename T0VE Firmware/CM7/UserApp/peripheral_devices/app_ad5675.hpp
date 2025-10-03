/*
 * mcp4728.hpp
 *
 *  Created on: Jun 13, 2025
 *      Author: govis
 */

#pragma once

#include "app_proctypes.hpp"
#include <array>
#include <span>

#include "app_utils.hpp" //for callback function
#include "app_regmap_helpers.hpp"
#include "app_threading.hpp" //for atomic variable

#include "app_hal_i2c.hpp"

class AD5675 {
private:
	Aux_I2C& bus; //reference to the i2c bus hardware

	/*
	 * `tx_complete()`
	 * checks the error flag, releases the I2C bus, and calls the registered TX error handler if applicable
	 */
	void tx_complete();

	/*
	 * `rx_complete()
	 * checks the error flag, releases the I2C bus, and calls the registered RX error handler if applicable
	 */
	void rx_complete();

	//callback functions that get invoked when we have a bus transmit or receive error
	//KEEP THESE LIGHTWEIGHT - THEY ARE CALLED IN ISR CONTEXT
	Callback_Function<> write_error_cb;
	Callback_Function<> read_error_cb;
	Callback_Function<> read_complete_cb;

	//flag that holds whether the device is present on the bus
	//if the device is absent, functions will just return to reduce bus overhead
	bool device_present;

	//and have a little thread signal that signals the non-ISR thread that our write has completed
	//have both a write complete and write error flag
	Thread_Signal transfer_complete_flag;
	Atomic_Var<bool> transfer_success;

	//========================== REGISTER DEFINITIONS ==============================

	const uint8_t ADDRESS_AD5675; //initialized in constructor

	//a little transmit buffer that we can point all of our regmap fields to
	std::array<uint8_t, 4> tx_buffer = {0};

	//some common register maps across all flavors of commands
	Regmap_Field command_code = {0, 4, 4, true, tx_buffer};
	Regmap_Field channel_sel = {0, 0, 3, true, tx_buffer};

	//### command we'll use for normal register writes
	static const uint8_t WRITE_UPDATE_n_COMMAND = 0b0011;
	static const size_t WRITE_UPDATE_n_LENGTH = 3; // 3 bytes to write/update a channel
	Regmap_Field dac_val = {2, 0, 16, true, tx_buffer};

	//### commands we'll use for power control
	enum Power_Control : uint8_t {
		POWER_UP = 0,
		POWER_DOWN_1KPD = 1,
		POWER_DOWN_HIZ = 3,
	};
	static const uint8_t POWER_CONTROL_COMMAND = 0b0100;
	static const size_t POWER_CONTROL_LENGTH = 3; // 3 bytes to a power control command
	std::array<Regmap_Field, 8> pwr_ctrl_bits = {
		Regmap_Field(2, 0, 2, true, tx_buffer),
		Regmap_Field(2, 2, 2, true, tx_buffer),
		Regmap_Field(2, 4, 2, true, tx_buffer),
		Regmap_Field(2, 6, 2, true, tx_buffer),
		Regmap_Field(1, 0, 2, true, tx_buffer),
		Regmap_Field(1, 2, 2, true, tx_buffer),
		Regmap_Field(1, 4, 2, true, tx_buffer),
		Regmap_Field(1, 6, 2, true, tx_buffer),
	};
	void configure_power_control();	//function that actually sets this up
	void power_control_complete();	//use as callback function

	//### commands we'll use for software reset
	static const uint8_t SOFTWARE_RESET_COMMAND = 0b0110;
	static const size_t SOFTWARE_RESET_LENGTH = 3; // 3 bytes to a software reset command
	static const uint16_t SOFTWARE_RESET_CODE = 0x1234;
	Regmap_Field soft_reset_bits = {2, 0, 16, true, tx_buffer};
	void do_soft_reset();			//function that performs soft reset
	void soft_reset_complete();		//use as callback function

	//### commands we'll use to initiate a device readback
	//note, only shift one byte
	static const uint8_t READBACK_SETUP_COMMAND = 0b1001;
	static const size_t READBACK_SETUP_LENGTH = 3; //EDIT: THREE byte to setup a read!!! https://ez.analog.com/data_converters/precision_dacs/f/q-a/26806/i2c-read-operation-on-ad5675r
	static const size_t READACK_SETUP_RECEIVE_LENGTH = 16; //read 8 channels * 2 bytes
	Atomic_Var<std::array<uint8_t, READACK_SETUP_RECEIVE_LENGTH>> readback_bytes; //thread-safe buffer to read back DAC setpoint bytes
	std::array<Regmap_Field, 8> dac_readback_vals = {
		Regmap_Field(1, 0, 16, true, tx_buffer),
		Regmap_Field(3, 0, 16, true, tx_buffer),
		Regmap_Field(5, 0, 16, true, tx_buffer),
		Regmap_Field(7, 0, 16, true, tx_buffer),
		Regmap_Field(9, 0, 16, true, tx_buffer),
		Regmap_Field(11, 0, 16, true, tx_buffer),
		Regmap_Field(13, 0, 16, true, tx_buffer),
		Regmap_Field(15, 0, 16, true, tx_buffer),
	};
	void service_readback();		//use as a callback function

public:
	//============================== TYPEDEFS =============================

	//I know this is kinda stupid, but makes explicit what our address options are
	enum I2C_Address : uint8_t {
		AD5675_0x0C = 0x0C,
		AD5675_0x0D = 0x0D,
		AD5675_0x0E = 0x0E,
		AD5675_0x0F = 0x0F,
	};

	static const uint32_t CONVERTER_RESOLUTION = 65536;

	//=====================================================================

	/*
	 * Constructor
	 * takes a hardware I2C bus reference and some other parameters to
	 */
	AD5675(Aux_I2C& _bus, const I2C_Address addr):
		bus(_bus), ADDRESS_AD5675(addr)
	{}

	/*
	 * `init()`
	 * - initializes the I2C bus
	 * - run `check_presence()`
	 * - software resets the DAC
	 * - puts the DAC in power-up mode
	 */
	void init();

	/*
	 * `deinit()`
	 * - pretty much just deinitializes the I2C bus
	 * - useful for when I2C bus needs to get suspended due to power rail disable
	 */
	void deinit();

	/*
	 * `check_presence()`
	 * - checks whether I2C address is ACKed using I2C driver class
	 * - returns `true` if present false if not present or bus error, stores a value to an internal variable
	 * - BLOCK with this function, i.e. wait until DMA transmission completes
	 */
	bool check_presence();

	/*
	 * 'write_channel()'
	 * - writes a 16-bit value to a single DAC channel
	 * - returns `true` if the transmission was performed, `false` if it wasn't
	 * - will call the `tx_error_cb` if there was any issue with the transmission
	 */
	bool write_channel(uint8_t channel, uint16_t val, Callback_Function<> _write_error_cb);

	/*
	 * `read_update_status()`
	 * - reads 16-byte status packet from IC via DMA
	 * - returns `true` if the transmission was performed, `false` if it wasn't
	 * - will call the `_read_error_cb` if there was any issue with the transmission and `_read_complete_cb` if successful
	 */
	 bool start_dac_readback(Callback_Function<> _read_complete_cb, Callback_Function<> _read_error_cb);
	 std::array<uint16_t, 8> dac_readback();

	//================= THINGS TO DELETE =================
	//delete assignment operator and copy constructor
	AD5675(AD5675 const& other) = delete;
	void operator=(AD5675 const& other) = delete;
};

