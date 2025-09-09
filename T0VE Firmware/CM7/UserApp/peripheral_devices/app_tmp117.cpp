

#include "app_tmp117.hpp"
#include "app_hal_tick.hpp"
#include "app_utils.hpp"
#include <cstring> //for memcpy

/*
 * Constructor
 * take a reference to the I2C bus; also takes configuration information via a config struct
 */
TMP117::TMP117(Aux_I2C& _bus, TMP117_Configuration_t _config):
	bus(_bus), config(_config)
{}

//============================== PUBLIC FUNCTIONS ==============================
void TMP117::init() {
	//initialize the bus
	bus.init();

	//check if the device is present; update the local variable
	device_present = check_presence();

	//soft reset the device
	soft_reset();

	//load in our desired configuration
	load_configuration();

	//read the device ID
	request_device_ID();
}

void TMP117::deinit() {
	//de-initialize the bus
	//and that's all we have to do really
	//call `init()` to reconfigure everything properly
	bus.deinit();
}

bool TMP117::check_presence() {
	//push the task down to the I2C driver
	//automatically claims the I2C bus mutex
	return bus.is_device_present(config.dev_addr);
}

uint16_t TMP117::get_device_ID() {
	//copy the device ID bytes into something local
	std::array<uint8_t, 2> _device_id_bytes = device_id_bytes;

	//point the decoder to our local
	device_id_decode.repoint(_device_id_bytes);

	//and decode the device ID using our decoder
	//cast to uint16_t performs this decode
	return device_id_decode;
}

float TMP117::read_temperature() {
	//copy the temperature bytes into something local
	std::array<uint8_t, 2> _temp_bytes = temp_bytes;

	//point the decoder to our local
	temp_decode.repoint(_temp_bytes);

	//build the temperature in device units using our decoder
	//cast to uint16_t automatically performs the conversion
	//have to `memcpy` because of unsafe uint --> int cast
	uint16_t unsigned_temp_device_units = temp_decode;
	int16_t signed_temp_device_units;
	memcpy(&signed_temp_device_units, &unsigned_temp_device_units, sizeof(unsigned_temp_device_units));

	//and convert to real world units using conversion factor
	return (float)signed_temp_device_units * TEMP_PER_BITS;
}

bool TMP117::start_read_temperature(Callback_Function<> _read_complete_cb, Callback_Function<> _read_error_cb) {
	//if the device isn't present on the I2C bus
	if(!device_present) {
		_read_error_cb(); //report an error
		return true; //don't try to reschedule
	}

	//save our error and completion callbacks right before we fire off this transmission
	read_error_cb = _read_error_cb;
	read_complete_cb = _read_complete_cb;

	//then clear the TX buffer, and set the first byte to the temperature register
	tx_buffer = {0};
	tx_buffer[0] = TEMP_REG_ADDRESS;

	//configure and fire the transmission
	//will read the bytes/release the bus in the rx_complete callback
	auto result = bus.write_read(	config.dev_addr,
									section(tx_buffer, 0, 1),
									2,
									BIND_CALLBACK(this, service_read_temperature));

	//and act appropriately depending on if the transmission went through
	if(result == Aux_I2C::I2C_STATUS::I2C_OK_READY) return true; //everything A-ok, transfer scheduled
	else if(result == Aux_I2C::I2C_STATUS::I2C_BUSY) return false; //peripheral was busy, transfer not scheduled
	else { //there was some kinda bus error
		read_error_cb(); 	//call the error callback
		return true;		//don't try to reschedule
	}
}

//============================== PRIVATE FUNCTIONS USED FOR BEHIND-THE-SCENES I2C TRANSMISSION ==============================

//############ TEMPERATURE ############
void TMP117::service_read_temperature() {
	//if we had a bus error, quickly run the provided receive error handler
	if(bus.was_bus_success() == Aux_I2C::I2C_STATUS::I2C_ERROR)
		read_error_cb();
	else {
		//atomically copy over just the receive buffer
		Aux_I2C::I2C_STATUS st;
		temp_bytes.with([&](std::array<uint8_t, 2>& _temp_bytes) {
			st = bus.retrieve(_temp_bytes);
		});

		//call the error callback if there was some issue with retrieving the bytes
		//otherwise call the user completion callback
		if(st != Aux_I2C::I2C_STATUS::I2C_OK_READY) read_error_cb();
		else read_complete_cb();
	}
}

//############ SOFT RESET ###########
void TMP117::reset_complete() {
	//check if the reset command was dispatched without issue
	transfer_success = bus.was_bus_success() == Aux_I2C::I2C_STATUS::I2C_OK_READY;

	//signal that we finished the transfer
	transfer_complete_flag.signal();
}

//blocking call that holds onto the I2C bus until transfer completes and we have a soft-reset
void TMP117::soft_reset() {
	//if we don't have a device on the bus, just return
	if(!device_present) return;

	//start by clearing the TX buffer, set the first byte to point to the config register,
	//then set our soft-reset bit in the correct spot of the TX buffer
	tx_buffer = {0};
	tx_buffer[0] = CONFIG_REG_ADDRESS;
	do_soft_reset = 1;

	//clear our signal flag, and run the transfer on the I2C BUS
	//tx_buffer is exactly 3 bytes, don't need to section it
	transfer_complete_flag.clear();
	Aux_I2C::I2C_STATUS status;
	do {
		status = bus.write(config.dev_addr, tx_buffer, BIND_CALLBACK(this, reset_complete));
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

	//finally wait 2ms for the reset to actually complete
	Tick::delay_ms(2);
}

//############ LOAD CONFIGURATION ###########

void TMP117::load_configuration_complete() {
	//check if the configuration register write was performed successfully
	transfer_success = bus.was_bus_success() == Aux_I2C::I2C_STATUS::I2C_OK_READY;

	//signal that we finished the transfer
	transfer_complete_flag.signal();
}

void TMP117::load_configuration() {
	//if we don't have a device on the bus, just return
	if(!device_present) return;

	//start by clearing the TX buffer, set the first byte to point to the config register,
	tx_buffer = {0};
	tx_buffer[0] = CONFIG_REG_ADDRESS;

	//copy over our configuration
	conversion_mode = config.sampling_config;
	conversion_cycles = config.conversion_rate_config;
	averaging_mode = config.averaging_config;
	therm_nalert_mode = config.alert_mode_config;
	alert_polarity = config.alert_polarity_config;
	dr_alert_mode = config.alert_source_config;

	//clear our signal flag, and run the transfer on the I2C BUS
	//tx_buffer is exactly 3 bytes, don't need to section it
	transfer_complete_flag.clear();
	Aux_I2C::I2C_STATUS status;
	do {
		status = bus.write(config.dev_addr, tx_buffer, BIND_CALLBACK(this, load_configuration_complete));
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

//############ DEVICE ID ###########
void TMP117::service_device_ID() {
	//check that the entire transmission went off without a hitch
	transfer_success = bus.was_bus_success() == Aux_I2C::I2C_STATUS::I2C_OK_READY;

	//if it did, then read the receive buffer into the device ID
	if(transfer_success) {
		//atomically copy over just the receive buffer
		Aux_I2C::I2C_STATUS st;
		device_id_bytes.with([&](std::array<uint8_t, 2>& _device_id_bytes) {
			st = bus.retrieve(_device_id_bytes);
		});

		//clear the transfer success flag if the decode wasn't successful for whatever reason
		if(st != Aux_I2C::I2C_STATUS::I2C_OK_READY) transfer_success = false;
	}

	//signal that the transmission is done
	transfer_complete_flag.signal();
}

void TMP117::request_device_ID() {
	//if we don't have a device on the bus, just return
	if(!device_present) return;

	//start by clearing the TX buffer, and set the first byte to the device ID register
	tx_buffer = {0};
	tx_buffer[0] = DEVICE_ID_REG_ADDRESS;

	//clear our signal flag, and run the transfer on the I2C BUS
	transfer_complete_flag.clear();
	Aux_I2C::I2C_STATUS status;
	do {
		status = bus.write_read(config.dev_addr,
								section(tx_buffer, 0, 1),
								2,
								BIND_CALLBACK(this, service_device_ID));
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
