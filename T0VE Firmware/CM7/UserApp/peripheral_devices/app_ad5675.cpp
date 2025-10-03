
#include "app_ad5675.hpp"


//============================== PUBLIC FUNCTIONS ==============================
void AD5675::init() {
	//initialize the bus
	bus.init();

	//check if the device is present; update the local variable
	device_present = check_presence();

	//soft reset the device
	//TODO: device behaves kinda weird on the I2C bus
	//disabling this for now, see if ADI has recommendations on how to fix
	//do_soft_reset();

	//and power up the DAC
	configure_power_control();
}

void AD5675::deinit() {
	//de-initialize the bus
	//and that's all we have to do really
	//call `init()` to reconfigure everything properly
	bus.deinit();
}

bool AD5675::check_presence() {
	//push the task down to the I2C driver
	//automatically claims the I2C bus mutex
	return bus.is_device_present(ADDRESS_AD5675);
}

bool AD5675::write_channel(uint8_t channel, uint16_t val, Callback_Function<> _write_error_cb) {
	//if the device isn't present on the I2C bus
	if(!device_present) {
		_write_error_cb(); //report an error
		return true; //don't try to reschedule
	}

	//start by preparing the TX buffer
	//place the appropriate channel and value in
	tx_buffer = {0};
	command_code = WRITE_UPDATE_n_COMMAND;
	channel_sel = channel; //automatically masks
	dac_val = val;

	//save our error callback right before we fire off this transmission
	write_error_cb = _write_error_cb;

	//configure and fire the transmission
	//will release the bus in the tx_complete callback
	auto result = bus.write(ADDRESS_AD5675,
							section(tx_buffer, 0, WRITE_UPDATE_n_LENGTH),
							BIND_CALLBACK(this, tx_complete));

	//and act appropriately depending on if the transmission went through
	if(result == Aux_I2C::I2C_STATUS::I2C_OK_READY) return true; //everything A-ok, transfer scheduled
	else if(result == Aux_I2C::I2C_STATUS::I2C_BUSY) return false; //peripheral was busy, transfer not scheduled
	else { //there was some kinda bus error
		write_error_cb(); 	//call the error callback
		return true;		//don't try to reschedule
	}
}

bool AD5675::start_dac_readback(Callback_Function<> _read_complete_cb, Callback_Function<> _read_error_cb) {
	//if the device isn't present on the I2C bus
	if(!device_present) {
		_read_error_cb(); //report an error
		return true; //don't try to reschedule
	}

	//save our error and completion callbacks right before we fire off this transmission
	read_error_cb = _read_error_cb;
	read_complete_cb = _read_complete_cb;

	//then clear the TX buffer
	//command a readback, set the starting channel to channel 0
	tx_buffer = {0};
	command_code = READBACK_SETUP_COMMAND;
	channel_sel = 0;

	//configure and fire the transmission
	//will read the bytes/release the bus in the rx_complete callback
	auto result = bus.write_read(	ADDRESS_AD5675,
									section(tx_buffer, 0, READBACK_SETUP_LENGTH),
									READACK_SETUP_RECEIVE_LENGTH,
									BIND_CALLBACK(this, service_readback));

	//and act appropriately depending on if the transmission went through
	if(result == Aux_I2C::I2C_STATUS::I2C_OK_READY) return true; //everything A-ok, transfer scheduled
	else if(result == Aux_I2C::I2C_STATUS::I2C_BUSY) return false; //peripheral was busy, transfer not scheduled
	else { //there was some kinda bus error
		read_error_cb(); 	//call the error callback
		return true;		//don't try to reschedule
	}
}

std::array<uint16_t, 8> AD5675::dac_readback() {
	std::array<uint16_t, 8> channel_vals; //create an output temporary

	//atomically copy over our readback bytes to another temporary
	std::array<uint8_t, READACK_SETUP_RECEIVE_LENGTH> _readback_bytes = readback_bytes;

	//now reassociate our parsers and pull out the channel-wise values
	for(size_t i = 0; i < dac_readback_vals.size(); i++) {
		dac_readback_vals[i].repoint(_readback_bytes);
		channel_vals[i] = (uint16_t)dac_readback_vals[i].read();
	}

	//return our decoded status structure
	return channel_vals;
}

//============================ PRIVATE FUNCTION DEFS ===========================
void AD5675::power_control_complete() {
	//check if the configuration register write was performed successfully
	transfer_success = bus.was_bus_success() == Aux_I2C::I2C_STATUS::I2C_OK_READY;

	//signal that we finished the transfer
	transfer_complete_flag.signal();
}

void AD5675::configure_power_control() {
	//if we don't have a device on the bus, just return
	if(!device_present) return;

	//start by clearing the TX buffer, say that we're updating the power control command
	tx_buffer = {0};
	command_code = POWER_CONTROL_COMMAND;

	//then power up all DAC channels
	for(Regmap_Field& bits : pwr_ctrl_bits)
		bits = Power_Control::POWER_UP;

	//clear our signal flag, and run the transfer on the I2C BUS
	//section the TX buffer to the correct length
	transfer_complete_flag.clear();
	Aux_I2C::I2C_STATUS status;
	do {
		status = bus.write(	ADDRESS_AD5675,
							section(tx_buffer, 0, POWER_CONTROL_LENGTH),
							BIND_CALLBACK(this, power_control_complete));
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

void AD5675::soft_reset_complete() {
	//check if the configuration register write was performed successfully
	transfer_success = bus.was_bus_success() == Aux_I2C::I2C_STATUS::I2C_OK_READY;

	//signal that we finished the transfer
	transfer_complete_flag.signal();
}

void AD5675::do_soft_reset() {
	//if we don't have a device on the bus, just return
	if(!device_present) return;

	//start by clearing the TX buffer, say that we're pointing to the software reset
	tx_buffer = {0};
	command_code = SOFTWARE_RESET_COMMAND;
	soft_reset_bits = SOFTWARE_RESET_CODE;

	//clear our signal flag, and run the transfer on the I2C BUS
	//section the TX buffer to the correct length
	transfer_complete_flag.clear();
	Aux_I2C::I2C_STATUS status;
	do {
		status = bus.write(	ADDRESS_AD5675,
							section(tx_buffer, 0, SOFTWARE_RESET_LENGTH),
							BIND_CALLBACK(this, soft_reset_complete));
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

//########## TX/RX CALLBACK HANDLERS #########
void AD5675::tx_complete() {
	//if we had a bus error, quickly run the provided transmit error handler
	if(bus.was_bus_success() == Aux_I2C::I2C_STATUS::I2C_ERROR)
		write_error_cb();
}

void AD5675::service_readback() {
	//if we had a bus error, quickly run the provided receive error handler
	if(bus.was_bus_success() == Aux_I2C::I2C_STATUS::I2C_ERROR)
		read_error_cb();
	else {
		//atomically copy over just the receive buffer
		Aux_I2C::I2C_STATUS st;
		readback_bytes.with([&](std::array<uint8_t, READACK_SETUP_RECEIVE_LENGTH>& _readback_bytes) {
			st = bus.retrieve(_readback_bytes);
		});

		//call the error callback if there was some issue with retrieving the bytes
		//otherwise call the user completion callback
		if(st != Aux_I2C::I2C_STATUS::I2C_OK_READY) read_error_cb();
		else read_complete_cb();
	}
}
