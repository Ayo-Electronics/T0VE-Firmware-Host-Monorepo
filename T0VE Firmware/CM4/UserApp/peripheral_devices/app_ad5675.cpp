
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

bool AD5675::write_channel(uint8_t channel, uint16_t val, Thread_Signal* _write_error_signal) {
	//if the device isn't present on the I2C bus
	if(!device_present) {
		if(_write_error_signal) _write_error_signal->signal(); //signal an error
		return true; //don't try to reschedule
	}

	//start by preparing the TX buffer
	//place the appropriate channel and value in
	tx_buffer = {0};
	command_code = WRITE_UPDATE_n_COMMAND;
	channel_sel = channel; //automatically masks
	dac_val = val;

	//configure and fire the transmission
	//will release the bus in the tx_complete callback
	auto result = bus.write(ADDRESS_AD5675,
							section(tx_buffer, 0, WRITE_UPDATE_n_LENGTH),
							nullptr, _write_error_signal);	//assume success, signal if error

	//and act appropriately depending on if the transmission went through
	if(result == Aux_I2C::I2C_STATUS::I2C_OK_READY) return true; //everything A-ok, transfer scheduled
	else if(result == Aux_I2C::I2C_STATUS::I2C_BUSY) return false; //peripheral was busy, transfer not scheduled
	else { //there was some kinda bus error
		if(_write_error_signal) _write_error_signal->signal(); //signal an error
		return true;		//don't try to reschedule
	}
}

//TODO: figure out how to read the readback bytes after transfer completion
bool AD5675::start_dac_readback(Thread_Signal* _read_complete_signal, Thread_Signal* _read_error_signal) {
	//if the device isn't present on the I2C bus
	if(!device_present) {
		if(_read_error_signal) _read_error_signal->signal(); //signal an error
		return true; //don't try to reschedule
	}

	//then clear the TX buffer
	//command a readback, set the starting channel to channel 0
	tx_buffer = {0};
	command_code = READBACK_SETUP_COMMAND;
	channel_sel = 0;

	//configure and fire the transmission
	auto result = bus.write_read(	ADDRESS_AD5675,
									section(tx_buffer, 0, READBACK_SETUP_LENGTH), readback_bytes,
									_read_complete_signal, _read_error_signal);

	//and act appropriately depending on if the transmission went through
	if(result == Aux_I2C::I2C_STATUS::I2C_OK_READY) return true; //everything A-ok, transfer scheduled
	else if(result == Aux_I2C::I2C_STATUS::I2C_BUSY) return false; //peripheral was busy, transfer not scheduled
	else { //there was some kinda bus error
		if(_read_error_signal) _read_error_signal->signal(); //signal an error
		return true;		//don't try to reschedule
	}
}

//NOTE: `dac_readback()` should only be called when i2c transaction is not in progress!
//i.e. only after a thread signal of completion and the start of the next dac readback!
std::array<uint16_t, 8> AD5675::dac_readback() {
	std::array<uint16_t, 8> channel_vals; //create an output temporary

	//copy over our readback bytes to another temporary
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
void AD5675::configure_power_control() {
	//if we don't have a device on the bus, just return
	if(!device_present) return;

	//start by clearing the TX buffer, say that we're updating the power control command
	tx_buffer = {0};
	command_code = POWER_CONTROL_COMMAND;

	//then power up all DAC channels
	for(Regmap_Field& bits : pwr_ctrl_bits)
		bits = Power_Control::POWER_UP;

	//set up some listeners for our signals, and run the I2C transfer until it succeeds
	auto listen_complete = internal_transfer_complete.listen();
	auto listen_error = internal_transfer_error.listen();
	Aux_I2C::I2C_STATUS status;
	do {
		status = bus.write(	ADDRESS_AD5675,
							section(tx_buffer, 0, POWER_CONTROL_LENGTH),
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

void AD5675::do_soft_reset() {
	//if we don't have a device on the bus, just return
	if(!device_present) return;

	//start by clearing the TX buffer, say that we're pointing to the software reset
	tx_buffer = {0};
	command_code = SOFTWARE_RESET_COMMAND;
	soft_reset_bits = SOFTWARE_RESET_CODE;

	//set up some listeners for our signals, and run the I2C transfer until it succeeds
	auto listen_complete = internal_transfer_complete.listen();
	auto listen_error = internal_transfer_error.listen();
	Aux_I2C::I2C_STATUS status;
	do {
		status = bus.write(	ADDRESS_AD5675,
							section(tx_buffer, 0, SOFTWARE_RESET_LENGTH),
							&internal_transfer_complete, &internal_transfer_error);
	} while(status == Aux_I2C::I2C_STATUS::I2C_BUSY); //stall until the transmission completes

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
