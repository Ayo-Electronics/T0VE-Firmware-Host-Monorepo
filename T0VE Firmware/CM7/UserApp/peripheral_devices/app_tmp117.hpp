/*
 * TMP117 I2C driver class
 */


#pragma once

#include "app_utils.hpp" //for callback function
#include "app_types.hpp"
#include "app_regmap_helpers.hpp"
#include "app_hal_i2c.hpp"

class TMP117 {
public:
	//============================== TYPEDEFS =============================

	//I know this is kinda stupid, but makes explicit what our address options are
	typedef enum {
		TMP117_ADDR_0x48 = 0x48,
		TMP117_ADDR_0x49 = 0x49,	//this is how the CoBs are wired
		TMP117_ADDR_0x4A = 0x4A,
		TMP117_ADDR_0x4B = 0x4B
	} TMP117_Addr_t;

	typedef enum {
		TMP117_MODE_CONTINUOUS = 0b00,
		TMP117_MODE_ONESHOT = 0b11,
		TMP117_MODE_SHUTDOWN = 0b01
	} TMP117_Sampling_t;

	typedef enum {
		TMP117_AVERAGING_NONE = 0b00,
		TMP117_AVERAGING_8 = 0b01,		//go with this + CONV[2:0] = 0b010
		TMP117_AVERAGING_32 = 0b10,
		TMP117_AVERAGING_64 = 0b10,
	} TMP117_Averaging_t;

	typedef enum {
		TMP117_MODE_ALERT = 0,
		TMP117_MODE_THERM = 1,
	} TMP117_Alert_t;

	typedef enum {
		TMP117_ALERT_ACTIVE_HIGH = 1,
		TMP117_ALERT_ACTIVE_LOW = 0,
	} TMP117_Alert_Pol_t;

	typedef enum {
		TMP117_ALERT_DRDY = 1,
		TMP117_ALERT_FLAGS = 0,
	} TMP117_Alert_Source_t;

	typedef struct {
		TMP117_Addr_t dev_addr;
		TMP117_Sampling_t sampling_config;
		uint8_t conversion_rate_config; //0-7, see datasheet for info/impact on sample rate
		TMP117_Averaging_t averaging_config;
		TMP117_Alert_t alert_mode_config;
		TMP117_Alert_Pol_t alert_polarity_config;
		TMP117_Alert_Source_t alert_source_config;
	} TMP117_Configuration_t;

	//========================== PUBLIC FUNCTIONS ===========================

	//constructor/destructor
	//just pass config struct with all the relevant information + the I2C bus we're running on
	TMP117(Aux_I2C& _bus, TMP117_Configuration_t _config);

	//delete copy constructor and assignment operator
	TMP117(const TMP117& other) = delete;
	void operator=(const TMP117& other) = delete;

	/*
	 * Initialize everything
	 *  \--> initialize the bus
	 *  \--> check if the device is present
	 *  \--> soft reset the device
	 *  \--> load in our desired configuration
	 *  \--> read device ID
	 */
	void init();

	/*
	 * Deinitialize things
	 * 	\--> mostly just the I2C bus
	 * 	\--> useful for when the I2C bus needs to go down due to regulator disable
	 */
	void deinit();

	/*
	 * Checks whether the TMP117 is on the I2C bus.
	 * BLOCK with this function, i.e. don't return until device detection routine concludes
	 * Returns boolean value if present and also updates internal status variables
	 *  \--> returns true if it is
	 *  \--> false if it didn't ACK its address
	 */
	bool check_presence();

	/*
	 * Gets the device ID of the TMP117 as read from the initialization function
	 *  \--> call `init()` again if you want a refreshed version of device ID
	 */
	uint16_t get_device_ID();

	/*
	 * Function pair that requests for the temperature in Celsius
	 * Then actually gets the temperature in Celsius
	 *  \--> the split function is due to the non-blocking nature of these I2C transmissions
	 *  \--> `start_read_temperature` returns true if transmission scheduled successfully, false if transmission not scheduled
	 */
	bool start_read_temperature(Callback_Function<> _read_complete_cb, Callback_Function<> _read_error_cb);
	float read_temperature();

private:
	Aux_I2C& bus; //reference to the i2c bus hardware
	TMP117_Configuration_t config; //store all the configuration information for the temperature sensor here

	//some more utility functions we'll call in `init()`
	void load_configuration();
	void load_configuration_complete();

	void soft_reset();
	void reset_complete();

	void request_device_ID();
	void service_device_ID();

	//and a servicing function we'll call when our temperature read transmission finishes
	void service_read_temperature();


	//callback functions that get invoked when we have a bus transmit or receive error
	//KEEP THESE LIGHTWEIGHT - THEY ARE CALLED IN ISR CONTEXT
	//Callback_Function<> write_error_cb; //don't need this
	Callback_Function<> read_error_cb;
	Callback_Function<> read_complete_cb;

	//flag that holds whether the device is present on the bus
	//if the device is absent, functions will just return to reduce bus overhead
	bool device_present;

	//and have a little thread signal that signals the non-ISR thread that our write has completed
	//have both a write complete and write error flag
	Thread_Signal transfer_complete_flag;
	Atomic_Var<bool> transfer_success;

	//======================= REGISTER DEFINITIONS =========================
	//own a TX buffer which to stage transmissions
	//just needs to hold 3 bytes
	std::array<uint8_t, 3> tx_buffer = {0};

	//write to the configuration register
	static const size_t CONFIG_REG_ADDRESS = 0x01;
	Regmap_Field_8B high_alert = {1, 7, 1, tx_buffer};
	Regmap_Field_8B low_alert = {1, 6, 1, tx_buffer};
	Regmap_Field_8B conversion_mode = {1, 3, 2, tx_buffer};
	Regmap_Field_16B conversion_cycles = {2, 7, 3, 1, tx_buffer};
	Regmap_Field_8B averaging_mode = {2, 5, 2, tx_buffer};
	Regmap_Field_8B therm_nalert_mode = {2, 4, 1, tx_buffer};
	Regmap_Field_8B alert_polarity = {2, 3, 1, tx_buffer};
	Regmap_Field_8B dr_alert_mode = {2, 2, 1, tx_buffer};
	Regmap_Field_8B do_soft_reset = {2, 1, 1, tx_buffer};

	//read from the temperature register
	static constexpr float TEMP_PER_BITS = 7.8125e-3; //mdegC per bit
	static const uint8_t TEMP_REG_ADDRESS = 0x00;
	Regmap_Field_16B temp_decode = {1, 0, 16, 1, {}};
	Atomic_Var<std::array<uint8_t, 2>> temp_bytes; //store the raw temperature bytes directly, only decode when requested

	//read from the DEVICE ID register
	//only done during initialization
	static const uint8_t DEVICE_ID_REG_ADDRESS = 0x0F;
	Regmap_Field_16B device_id_decode = {1, 0, 16, 1, {}};
	Atomic_Var<std::array<uint8_t, 2>> device_id_bytes; //store the raw device ID bytes directly, only decode when requested
};
