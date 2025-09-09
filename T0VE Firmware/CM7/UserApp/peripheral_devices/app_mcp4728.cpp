/*
 * mcp4728.cpp
 *
 *  Created on: Jun 13, 2025
 *      Author: govis
 */

#include "app_mcp4728.hpp"

/*
 * Constructor
 * takes a hardware I2C bus reference and some other parameters to
 */
MCP4728::MCP4728(Aux_I2C& _bus, const MCP4728_Addr_t addr, const MCP4728_Vref_t vref, const MCP4728_Gain_t gain, const MCP4728_LDAC_t ldac):
	bus(_bus),
	write_error_cb(),									/* User callback function hooks (default initialization) */
	read_error_cb(),
	read_complete_cb(),
	ADDRESS_MCP4728(addr),							/* Register constants */
	VREF_MASK(vref),
	UDAC_MASK(ldac),
	GAIN_MASK(gain)
{}

/*
 * `init()`
 * - initializes the I2C bus
 * - run `check_presence()`
 */
void MCP4728::init() {
	//initialize the bus
	bus.init();

	//check if the device is on the bus, let the function update the internal flags
	check_presence();
}

/*
 * `deinit()`
 * - deinit the I2C bus
 */
void MCP4728::deinit() {
	bus.deinit();
}

/*
 * `check_presence()`
 * - checks whether I2C address is ACKed using I2C driver class
 * - returns `true` if present false if not present or bus error, stores a value to an internal variable
 * - BLOCK with this function, i.e. wait until DMA transmission completes
 */
bool MCP4728::check_presence() {
	//push the task down to the I2C driver
	//automatically claims the I2C bus mutex
	device_present = bus.is_device_present(ADDRESS_MCP4728);
	return device_present;
}

/*
 * 'write_channels()'
 * - per
 * - returns `true` if the I2C bus was free, `false` if it wasn't
 * - will call the `write_error_callback` if there was any issue with the transmission (or other faults/errors here)
 */
bool MCP4728::write_channels(std::array<uint16_t, 4> values, Callback_Function<> _write_error_cb) {
	//if the device isn't present on the I2C bus
	if(!device_present) {
		_write_error_cb(); //report an error
		return true; //don't try to reschedule
	}

	//start by preparing the TX buffer
	tx_buffer = {0};

	//load the `multi_write` instruction into the command code
	mwr_multi_write_command = MULTI_WRITE_COMMAND_CODE;

	//and now go through channel-by-channel and assign the correct bits
	for(size_t i = 0; i < 4; i++) {
		mwr_udac_bits[i] = UDAC_MASK; //set the UDAC bit to what we configured
		mwr_vref_source_bits[i] = VREF_MASK; //set the VREF bit to what we configured
		mwr_gain_bits[i] = GAIN_MASK; //set the gain bit to what we configured

		mwr_channel_sels[i] = i; //set the channel selector to the appropriate channel
		mwr_power_down_bits[i] = 0; //set the power down bit to 0 for normal operation

		mwr_dac_vals[i] = clip(values[i], (uint16_t)0, CONVERTER_RESOLUTION); //set the DAC value to the appropriate value, clipping it for known behavior
	}

	//save our error callback right before we fire off this transmission
	write_error_cb = _write_error_cb;

	//configure and fire the transmission
	//will release the bus in the tx_complete callback
	auto result = bus.write(ADDRESS_MCP4728, section(tx_buffer, 0, MULTI_WRITE_COMMAND_LENGTH), BIND_CALLBACK(this, tx_complete));

	//and act appropriately depending on if the transmission went through
	if(result == Aux_I2C::I2C_STATUS::I2C_OK_READY) return true; //everything A-ok, transfer scheduled
	else if(result == Aux_I2C::I2C_STATUS::I2C_BUSY) return false; //peripheral was busy, transfer not scheduled
	else { //there was some kinda bus error
		write_error_cb(); 	//call the error callback
		return true;		//don't try to reschedule
	}
}

/*
 * `write_channels_eeprom()`
 * -
 * - returns `true` if the I2C bus was free, `false` if it wasn't
 * - will call the `write_error_callback` if there was any issue with the transmission
 */
bool MCP4728::write_channels_eeprom(std::array<uint16_t, 4> values, Callback_Function<> _write_error_cb) {
	//if the device isn't present on the I2C bus
	if(!device_present) {
		_write_error_cb(); //report an error
		return true; //don't try to reschedule
	}
	
	//start by preparing the TX buffer
	tx_buffer = {0};

	//start with the command header, setting it to correct/configured values
	seqwr_sequential_write_command = SEQUENTIAL_WRITE_COMMAND_CODE;
	seqwr_start_channel_sel = 0;
	seqwr_udac = UDAC_MASK;

	for(size_t i = 0; i < 4; i++) {
		seqwr_vref_source_bits[i] = VREF_MASK; //write the VREF mask to what we configured
		seqwr_gain_bits[i] = GAIN_MASK; //write the gain mask to what we configured
		seqwr_power_down_bits[i] = 0; //write the power down bit to 0 for normal operation

		seqwr_dac_vals[i] = clip(values[i], (uint16_t)0, CONVERTER_RESOLUTION); //write the DAC value to the appropriate value, clipping it for known behavior
	}

	//save our error callback right before we fire off this transmission
	write_error_cb = _write_error_cb;

	//configure and fire the transmission
	//will release the bus in the tx_complete callback
	auto result = bus.write(ADDRESS_MCP4728, section(tx_buffer, 0, SEQUENTIAL_WRITE_COMMAND_LENGTH), BIND_CALLBACK(this, tx_complete));

	//and act appropriately depending on if the transmission went through
	if(result == Aux_I2C::I2C_STATUS::I2C_OK_READY) return true; //everything A-ok, transfer scheduled
	else if(result == Aux_I2C::I2C_STATUS::I2C_BUSY) return false; //peripheral was busy, transfer not scheduled
	else { //there was some kinda bus error
		write_error_cb(); 	//call the error callback
		return true;		//don't try to reschedule
	}
}

/*
 * `start_read_update_status()`
 * - starts the read update status process
 * - returns `true` if the transmission was performed, `false` if it wasn't
 * - will call the `read_error_callback` if there was any issue with the transmission
 */
bool MCP4728::start_read_update_status(Callback_Function<> _read_complete_cb, Callback_Function<> _read_error_cb) {

	//if the device isn't present on the I2C bus
	if(!device_present) {
		_read_error_cb(); //report an error
		return true; //don't try to reschedule
	}

	//save our error and completion callbacks right before we fire off this transmission
	read_error_cb = _read_error_cb;
	read_complete_cb = _read_complete_cb;

	//configure and fire the transmission
	//will read the bytes/release the bus in the rx_complete callback
	auto result = bus.read(ADDRESS_MCP4728, READ_COMMAND_LENGTH, BIND_CALLBACK(this, rx_complete));

	//and act appropriately depending on if the transmission went through
	if(result == Aux_I2C::I2C_STATUS::I2C_OK_READY) return true; //everything A-ok, transfer scheduled
	else if(result == Aux_I2C::I2C_STATUS::I2C_BUSY) return false; //peripheral was busy, transfer not scheduled
	else { //there was some kinda bus error
		read_error_cb(); 	//call the error callback
		return true;		//don't try to reschedule
	}
}

/*
 * `read_update_status()`
 * - returns the status information from the device
 */
MCP4728::MCP4728_Status_t MCP4728::read_update_status() {
	MCP4728_Status_t device_status; //create a temporary

	//atomically copy over our status bytes
	device_status.status_bytes = status_bytes;

	//and finish decoding the status bytes to useful DAC vals
	for(size_t i = 0; i < 4; i++) {
		//first repoint the registers to our temp
		devr_dac_vals[i].repoint(device_status.status_bytes);
		devr_eeprom_vals[i].repoint(device_status.status_bytes);

		//then actually pull the value out
		device_status.dac_vals[i] = (uint16_t)devr_dac_vals[i];
		device_status.eeprom_vals[i] = (uint16_t)devr_eeprom_vals[i];
	}

	//return our decoded status structure
	return device_status;
}

//================================= PRIVATE FUNCTION DEFINITIONS ===============================

void MCP4728::tx_complete() {
	//if we had a bus error, quickly run the provided transmit error handler
	if(bus.was_bus_success() == Aux_I2C::I2C_STATUS::I2C_ERROR)
		write_error_cb();
}

void MCP4728::rx_complete() {
	//if we had a bus error, quickly run the provided receive error handler
	if(bus.was_bus_success() == Aux_I2C::I2C_STATUS::I2C_ERROR)
		read_error_cb();
	else {
		//atomically copy over just the receive buffer
		Aux_I2C::I2C_STATUS st;
		status_bytes.with([&](std::array<uint8_t, READ_COMMAND_LENGTH>& _status_bytes) {
			st = bus.retrieve(_status_bytes);
		});

		//call the error callback if there was some issue with retrieving the bytes
		//otherwise call the user completion callback
		if(st != Aux_I2C::I2C_STATUS::I2C_OK_READY) read_error_cb();
		else read_complete_cb();
	}
}
