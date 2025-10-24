/*
 * mcp4728.hpp
 *
 *  Created on: Jun 13, 2025
 *      Author: govis
 */

#pragma once

#include <app_proctypes.hpp>
#include <array>
#include <span>

#include "app_utils.hpp" //for callback function
#include "app_regmap_helpers.hpp"
#include "app_threading.hpp" //for atomic variable

#include "app_hal_i2c.hpp"

class MCP4728 {
private:
	Aux_I2C& bus; //reference to the i2c bus hardware

	//flag that holds whether the device is present on the bus
	//if the device is absent, functions will just return to reduce bus overhead
	bool device_present;

	//and have a little thread signal that signals the non-ISR thread that our write has completed
	//have both a write complete and write error flag
	PERSISTENT((Thread_Signal), internal_transfer_complete);
	PERSISTENT((Thread_Signal), internal_transfer_error);

	//========================== REGISTER DEFINITIONS ==============================
	// --> write without EEPROM takes 12 bytes
	// --> write with EEPROM takes 9 bytes
	// --> read takes 24 bytes
	std::array<uint8_t, 16> tx_buffer = {};
	//std::array<uint8_t, 30> rx_buffer = {}; //don't need this--need to use the I2C library's RX buffer due to DMA memory accessibility
	//bytes will just get copied directly into the status structure

	const uint8_t ADDRESS_MCP4728; //initialized in constructor
	const uint8_t VREF_MASK; //initialized in constructor; assign/OR when necessary
	const uint8_t UDAC_MASK; //initialized in constructor; assign/OR when necessary
	const uint8_t GAIN_MASK; //initialized in constructor; assign/OR when necessary

	//#### for the multi-write without EEPROM ####
	static const uint8_t MULTI_WRITE_COMMAND_CODE = 0b01000;
	static const size_t MULTI_WRITE_COMMAND_LENGTH = 12;
	Regmap_Field mwr_multi_write_command = {0, 3, 5, true, tx_buffer}; //place the MULTI_WRITE_COMMAND_CODE in here
	std::array<Regmap_Field, 4> mwr_channel_sels = {	//set these corresponding to the particular channel we'd like to write to
		Regmap_Field(0, 1, 2, true, tx_buffer),
		Regmap_Field(3, 1, 2, true, tx_buffer),
		Regmap_Field(6, 1, 2, true, tx_buffer),
		Regmap_Field(9, 1, 2, true, tx_buffer),
	};
	std::array<Regmap_Field, 4> mwr_udac_bits = { //set these according to the UDAC configuration
		Regmap_Field(0, 0, 1, true, tx_buffer),
		Regmap_Field(3, 0, 1, true, tx_buffer),
		Regmap_Field(6, 0, 1, true, tx_buffer),
		Regmap_Field(9, 0, 1, true, tx_buffer),
	};
	std::array<Regmap_Field, 4> mwr_vref_source_bits = { //set these according to the VREF configuration
		Regmap_Field(1, 7, 1, true, tx_buffer),
		Regmap_Field(4, 7, 1, true, tx_buffer),
		Regmap_Field(7, 7, 1, true, tx_buffer),
		Regmap_Field(10, 7, 1, true, tx_buffer),
	};
	std::array<Regmap_Field, 4> mwr_power_down_bits = { //set these according to the power down configuration
		Regmap_Field(1, 5, 2, true, tx_buffer),
		Regmap_Field(4, 5, 2, true, tx_buffer),
		Regmap_Field(7, 5, 2, true, tx_buffer),
		Regmap_Field(10, 5, 2, true, tx_buffer),
	};
	std::array<Regmap_Field, 4> mwr_gain_bits = { //set these according to the gain configuration
		Regmap_Field(1, 4, 1, true, tx_buffer),
		Regmap_Field(4, 4, 1, true, tx_buffer),
		Regmap_Field(7, 4, 1, true, tx_buffer),
		Regmap_Field(10, 4, 1, true, tx_buffer),
	};
	std::array<Regmap_Field, 4> mwr_dac_vals = { //set these according to the DAC value
		Regmap_Field(2, 0, 12, true, tx_buffer),
		Regmap_Field(5, 0, 12, true, tx_buffer),
		Regmap_Field(8, 0, 12, true, tx_buffer),
		Regmap_Field(11, 0, 12, true, tx_buffer),
	};

	//#### for the multi-write with EEPROM ####
	//after the first byte, the pattern repeats for all four channels
	static const uint8_t SEQUENTIAL_WRITE_COMMAND_CODE = 0b01010;
	static const size_t SEQUENTIAL_WRITE_COMMAND_LENGTH = 9;
	Regmap_Field seqwr_sequential_write_command = {0, 3, 5, true, tx_buffer}; //place the SEQUENTIAL_WRITE_COMMAND_CODE in here
	Regmap_Field seqwr_start_channel_sel = {0, 1, 2, true, tx_buffer}; //set this to the first channel we'd like to write to in our sequence
	Regmap_Field seqwr_udac = {0, 0, 1, true, tx_buffer}; //set this to the UDAC configuration
	std::array<Regmap_Field, 4> seqwr_vref_source_bits = { //set these according to the VREF configuration
		Regmap_Field(1, 7, 1, true, tx_buffer),
		Regmap_Field(3, 7, 1, true, tx_buffer),
		Regmap_Field(5, 7, 1, true, tx_buffer),
		Regmap_Field(7, 7, 1, true, tx_buffer),
	};
	std::array<Regmap_Field, 4> seqwr_power_down_bits = { //set these according to the power down configuration
		Regmap_Field(1, 5, 2, true, tx_buffer),
		Regmap_Field(3, 5, 2, true, tx_buffer),
		Regmap_Field(5, 5, 2, true, tx_buffer),
		Regmap_Field(7, 5, 2, true, tx_buffer),
	};
	std::array<Regmap_Field, 4> seqwr_gain_bits = { //set these according to the gain configuration
		Regmap_Field(1, 4, 1, true, tx_buffer)	,
		Regmap_Field(3, 4, 1, true, tx_buffer),
		Regmap_Field(5, 4, 1, true, tx_buffer),
		Regmap_Field(7, 4, 1, true, tx_buffer),
	};
	std::array<Regmap_Field, 4> seqwr_dac_vals = { //set these according to the DAC value per channel
		Regmap_Field(2, 0, 12, true, tx_buffer),
		Regmap_Field(4, 0, 12, true, tx_buffer),
		Regmap_Field(6, 0, 12, true, tx_buffer),
		Regmap_Field(8, 0, 12, true, tx_buffer),
	};

	//###### for the entire chip read ######
	static const size_t READ_COMMAND_LENGTH = 24;
	std::array<uint8_t, READ_COMMAND_LENGTH> status_bytes;
	std::array<Regmap_Field, 4> devr_dac_vals = { //BUT only care about the assigned DAC values (need to repoint these to work!)
		Regmap_Field(2, 0, 12, true, {}),
		Regmap_Field(8, 0, 12, true, {}),
		Regmap_Field(14, 0, 12, true, {}),
		Regmap_Field(20, 0, 12, true, {}),
	};
	std::array<Regmap_Field, 4> devr_eeprom_vals { //and potentially the values in the DAC EEPROM (need to repoint these to work!)
		Regmap_Field(5, 0, 12, true, {}),
		Regmap_Field(11, 0, 12, true, {}),
		Regmap_Field(17, 0, 12, true, {}),
		Regmap_Field(23, 0, 12, true, {}),
	};
public:
	//============================== TYPEDEFS =============================

	//I know this is kinda stupid, but makes explicit what our address options are
	typedef enum {
		MCP4728_ADDRESS_0x60 = 0x60,
		MCP4728_ADDRESS_0x61 = 0x61,
		MCP4728_ADDRESS_0x62 = 0x62,
		MCP4728_ADDRESS_0x63 = 0x63,
		MCP4728_ADDRESS_0x64 = 0x64,
		MCP4728_ADDRESS_0x65 = 0x65,
		MCP4728_ADDRESS_0x66 = 0x66,
		MCP4728_ADDRESS_0x67 = 0x67,
	} MCP4728_Addr_t;

	typedef enum {
		MCP4728_VREF_EXT = 0,
		MCP4728_VREF_INT2p048 = 1,
	} MCP4728_Vref_t;

	typedef enum {
		MCP4728_GAIN_1x = 0,
		MCP4728_GAIN_2x = 1,
	} MCP4728_Gain_t;

	typedef enum {
		MCP4728_LDAC_LOW = 0,
		MCP4728_USING_LDAC = 1,
	} MCP4728_LDAC_t;

	typedef struct {
		std::array<uint16_t, 4> dac_vals;
		std::array<uint16_t, 4> eeprom_vals;
		std::array<uint8_t, READ_COMMAND_LENGTH> status_bytes;
	} MCP4728_Status_t;

	static const uint16_t CONVERTER_RESOLUTION = 4096;

	//=====================================================================

	/*
	 * Constructor
	 * takes a hardware I2C bus reference and some other parameters to
	 */
	MCP4728(Aux_I2C& _bus, const MCP4728_Addr_t addr, const MCP4728_Vref_t vref, const MCP4728_Gain_t gain, const MCP4728_LDAC_t ldac);

	/*
	 * `init()`
	 * - initializes the I2C bus
	 * - run `check_presence()`
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
	 * 'write_channels()'
	 * - per
	 * - returns `true` if the transmission was performed, `false` if it wasn't
	 * - will assert the `_write_error_signal` if there was any issue with the transmission
	 * - otherwise assume transmission was successful
	 */
	bool write_channels(std::array<uint16_t, 4> values, Thread_Signal* _write_error_signal);

	/*
	 * `write_channels_eeprom()`
	 * -
	 * - returns `true` if the transmission was performed, `false` if it wasn't
	 * - will assert the `_write_error_signal` if there was any issue with the transmission
	 * - otherwise assume transmission was successful
	 */
	bool write_channels_eeprom(std::array<uint16_t, 4> values, Thread_Signal* _write_error_signal);

	/*
	 * `read_update_status()`
	 * - reads 24-byte status packet from IC via DMA
	 * - pass a 4-element `span` into the function as well as a boolean completion flag & `status_valid` flag
	 * - returns `true` if the transmission was performed, `false` if it wasn't
	 * - will assert the `_read_error_signal` if there was any issue with the transmission
	 * - and otherwise assert the `_read_complete_signal` when data is ready
	 */
	 bool start_read_update_status(Thread_Signal* _read_complete_signal, Thread_Signal* _read_error_signal);
	 MCP4728_Status_t read_update_status();

	//================= THINGS TO DELETE =================
	//delete assignment operator and copy constructor
	MCP4728(MCP4728 const& other) = delete;
	void operator=(MCP4728 const& other) = delete;
};

/*
 * SCRATCH NOTES:
 *  - set PD bits to 0 for normal operation
 *  - since LDAC bit is tied low, can set the UDAC bit high
 */
