

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

bool TMP117::start_read_temperature(Thread_Signal* _read_complete_signal, Thread_Signal* _read_error_signal) {
	//if the device isn't present on the I2C bus
	if(!device_present) {
		if(_read_error_signal) _read_error_signal->signal(); //signal an error
		return true; //don't try to reschedule
	}

	//then clear the TX buffer, and set the first byte to the temperature register
	tx_buffer = {0};
	tx_buffer[0] = TEMP_REG_ADDRESS;

	//configure and fire the transmission
	//will read the bytes/release the bus in the rx_complete callback
	auto result = bus.write_read(	config.dev_addr,
									section(tx_buffer, 0, 1), temp_bytes,
									_read_complete_signal, _read_error_signal);

	//and act appropriately depending on if the transmission went through
	if(result == Aux_I2C::I2C_STATUS::I2C_OK_READY) return true; //everything A-ok, transfer scheduled
	else if(result == Aux_I2C::I2C_STATUS::I2C_BUSY) return false; //peripheral was busy, transfer not scheduled
	else { //there was some kinda bus error
		if(_read_error_signal) _read_error_signal->signal(); //signal an error
		return true;		//don't try to reschedule
	}
}

//============================== PRIVATE FUNCTIONS USED FOR BEHIND-THE-SCENES I2C TRANSMISSION ==============================

//blocking call that holds onto the I2C bus until transfer completes and we have a soft-reset
void TMP117::soft_reset() {
	//if we don't have a device on the bus, just return
	if(!device_present) return;

	//start by clearing the TX buffer, set the first byte to point to the config register,
	//then set our soft-reset bit in the correct spot of the TX buffer
	tx_buffer = {0};
	tx_buffer[0] = CONFIG_REG_ADDRESS;
	do_soft_reset = 1;

	//set up some listeners for our signals, and run the I2C transfer until it succeeds
	auto listen_complete = internal_transfer_complete.listen();
	auto listen_error = internal_transfer_error.listen();
	Aux_I2C::I2C_STATUS status;
	do {
		status = bus.write(	config.dev_addr, tx_buffer,
							&internal_transfer_complete, &internal_transfer_error);
	} while(status == Aux_I2C::I2C_STATUS::I2C_BUSY); //stall until the transmission completes

	//and check if the transmission went through
	if(status != Aux_I2C::I2C_STATUS::I2C_OK_READY) {
		device_present = false;
		return;
	}

	//wait for the transmission to complete, error, or timeout
	auto start_millis = Tick::get_ms();
	while(true) {
		//we time out
		if((Tick::get_ms() - start_millis) > 1000) {
			device_present = false;
			return;
		}

		//we receive an error
		if(listen_error.check()) {
			device_present = false;
			return;
		}

		//our read is actually complete, do the final step
		if(listen_complete.check()) break;
	}

	//finally wait 2ms for the reset to actually complete
	Tick::delay_ms(2);
}

//############ LOAD CONFIGURATION ###########

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

	//set up some listeners for our signals, and run the I2C transfer until it succeeds
	auto listen_complete = internal_transfer_complete.listen();
	auto listen_error = internal_transfer_error.listen();
	Aux_I2C::I2C_STATUS status;
	do {
		status = bus.write(	config.dev_addr, tx_buffer,
							&internal_transfer_complete, &internal_transfer_error);
	} while(status == Aux_I2C::I2C_STATUS::I2C_BUSY); //stall until the transmission completes

	//and check if the transmission went through
	if(status != Aux_I2C::I2C_STATUS::I2C_OK_READY) {
		device_present = false;
		return;
	}

	//wait for the transmission to complete, error, or timeout
	auto start_millis = Tick::get_ms();
	while(true) {
		//we time out
		if((Tick::get_ms() - start_millis) > 1000) {
			device_present = false;
			return;
		}

		//we receive an error
		if(listen_error.check()) {
			device_present = false;
			return;
		}

		//our read is actually complete, just return successfully
		if(listen_complete.check()) return;
	}
}

//############ DEVICE ID ###########
void TMP117::request_device_ID() {
	//if we don't have a device on the bus, just return
	if(!device_present) return;

	//start by clearing the TX buffer, and set the first byte to the device ID register
	tx_buffer = {0};
	tx_buffer[0] = DEVICE_ID_REG_ADDRESS;

	//set up some listeners for our signals, and run the I2C transfer until it succeeds
	auto listen_complete = internal_transfer_complete.listen();
	auto listen_error = internal_transfer_error.listen();
	Aux_I2C::I2C_STATUS status;
	do {
		status = bus.write_read(config.dev_addr,
								section(tx_buffer, 0, 1), device_id_bytes,
								&internal_transfer_complete, &internal_transfer_error);
	} while(status == Aux_I2C::I2C_STATUS::I2C_BUSY); //stall until the transmission completes

	//and check if the transmission went through
	if(status != Aux_I2C::I2C_STATUS::I2C_OK_READY) {
		device_present = false;
		return;
	}

	//wait for the transmission to complete, error, or timeout
	auto start_millis = Tick::get_ms();
	while(true) {
		//we time out
		if((Tick::get_ms() - start_millis) > 1000) {
			device_present = false;
			return;
		}

		//we receive an error
		if(listen_error.check()) {
			device_present = false;
			return;
		}

		//our read is actually complete, just return successfully
		if(listen_complete.check()) return;
	}
}
