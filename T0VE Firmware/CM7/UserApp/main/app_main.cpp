/*
 * app_main.cpp
 *
 *  Created on: Sep 12, 2023
 *      Author: Ishaan
 */

#include "app_main.hpp"

//========== DEBUG INCLUDES ==========
#include "app_debug_if.hpp"
#include "app_debug_protobuf.hpp"

//========== HAL INCLUDES ==========
#include "app_hal_gpio.hpp"
#include "app_hal_pin_mapping.hpp"
#include "app_hal_i2c.hpp"
#include "app_hal_spi.hpp"
#include "app_hal_dram.hpp"
#include "app_hal_pwm.hpp"
#include "app_hal_tick.hpp"
#include "app_hal_board_uid.hpp"

//========== USB INCLUDES ==========
#include "app_usb_if.hpp"
#include "app_msc_if.hpp"

//============ SUBSYSTEM INCLUDES ===========
#include "app_state_supervisor.hpp"
#include "app_power_monitor.hpp"
#include "app_adc_offset_ctrl.hpp"
#include "app_sync_if.hpp"
#include "app_hispeed_subsys.hpp"
#include "app_led_indicators.hpp"
#include "app_cob_temp_monitor.hpp"
#include "app_cob_eeprom.hpp"
#include "app_bias_drives.hpp"
#include "app_comms_subsys.hpp"

//========= UTILITY INCLUDES =========
#include "app_scheduler.hpp"
#include "app_string.hpp"

//============= INSTANTIATION OF HARDWARE ============
Aux_I2C i2c_bus(Aux_I2C::AUX_I2C_HARDWARE);
USB_Interface usb = USB_Interface(USB_Interface::USB_CHANNEL);
//MSC_Interface msc(usb, MSC_Interface::MSC_CHANNEL);

Hispeed_Subsystem::Hispeed_Channel_Hardware_t CHANNEL_0_HW = {
		._spi_channel_hw = HiSpeed_SPI::SPI_CHANNEL_0,
		._cs_dac_pin = Pin_Mapping::SPI_CS_DAC_CH0,
		._cs_adc_pin = Pin_Mapping::SPI_CS_ADC_CH0,
		._soa_en_pin = Pin_Mapping::SOA_EN_CH0,
		._tia_en_pin = Pin_Mapping::TIA_EN_CH0,
		._cs_dac_timer = PWM::CS_DAC_CH0_CHANNEL,
		._cs_adc_timer = PWM::CS_ADC_CH0_CHANNEL,
};
Hispeed_Subsystem::Hispeed_Channel_Hardware_t CHANNEL_1_HW = {
		._spi_channel_hw = HiSpeed_SPI::SPI_CHANNEL_1,
		._cs_dac_pin = Pin_Mapping::SPI_CS_DAC_CH1,
		._cs_adc_pin = Pin_Mapping::SPI_CS_ADC_CH1,
		._soa_en_pin = Pin_Mapping::SOA_EN_CH1,
		._tia_en_pin = Pin_Mapping::TIA_EN_CH1,
		._cs_dac_timer = PWM::CS_DAC_CH1_CHANNEL,
		._cs_adc_timer = PWM::CS_ADC_CH1_CHANNEL,
};
Hispeed_Subsystem::Hispeed_Channel_Hardware_t CHANNEL_2_HW = {
		._spi_channel_hw = HiSpeed_SPI::SPI_CHANNEL_2,
		._cs_dac_pin = Pin_Mapping::SPI_CS_DAC_CH2,
		._cs_adc_pin = Pin_Mapping::SPI_CS_ADC_CH2,
		._soa_en_pin = Pin_Mapping::SOA_EN_CH2,
		._tia_en_pin = Pin_Mapping::TIA_EN_CH2,
		._cs_dac_timer = PWM::CS_DAC_CH2_CHANNEL,
		._cs_adc_timer = PWM::CS_ADC_CH2_CHANNEL,
};
Hispeed_Subsystem::Hispeed_Channel_Hardware_t CHANNEL_3_HW = {
		._spi_channel_hw = HiSpeed_SPI::SPI_CHANNEL_3,
		._cs_dac_pin = Pin_Mapping::SPI_CS_DAC_CH3,
		._cs_adc_pin = Pin_Mapping::SPI_CS_ADC_CH3,
		._soa_en_pin = Pin_Mapping::SOA_EN_CH3,
		._tia_en_pin = Pin_Mapping::TIA_EN_CH3,
		._cs_dac_timer = PWM::CS_DAC_CH3_CHANNEL,
		._cs_adc_timer = PWM::CS_ADC_CH3_CHANNEL,
};

//=============== CREATING SYSTEM STATE AGGREGATION + LINKING SUBSYSTEM STATE VARIABLES ===============
State_Supervisor state_supervisor;

//============== INSTANTIATION OF DEVICES ==============
Multicard_Info multicard_info_subsys(	Pin_Mapping::SYNC_NID_0,
										Pin_Mapping::SYNC_NID_1,
										Pin_Mapping::SYNC_NID_2,
										Pin_Mapping::SYNC_NID_3,
										Pin_Mapping::PRES_INTLK,
										Pin_Mapping::PD_SEL_PIC,
										Pin_Mapping::PD_SEL_AUX,
										Pin_Mapping::SYNC_NODE_READY,
										Pin_Mapping::SYNC_ALL_READY,
										PWM::SYNCOUT_TIMER,
										PWM::SYNCIN_TIMER);
Power_Monitor pm_onboard_subsys(Pin_Mapping::PWR_REG_EN, Pin_Mapping::PWR_PGOOD, 50); //onboard analog regulators
Power_Monitor pm_motherboard_subsys(Pin_Mapping::EXT_PWR_REG_EN, Pin_Mapping::EXT_PWR_PGOOD, 50); //offboard analog regulators
ADC_Offset_Control offset_ctrl_subsys(i2c_bus);
CoB_Temp_Monitor cob_temp_monitor(i2c_bus);
CoB_EEPROM cob_eeprom(i2c_bus);
Waveguide_Bias_Drive wgbias_subsys(i2c_bus, Pin_Mapping::BIAS_DRIVE_EN, Pin_Mapping::BIAS_DAC_RESET);

Hispeed_Subsystem hispeed_subsys(	CHANNEL_0_HW,
									CHANNEL_1_HW,
									CHANNEL_2_HW,
									CHANNEL_3_HW,
									DRAM::DRAM_INTERFACE,
									pm_onboard_subsys,
									multicard_info_subsys);

LED_Indicators indicators_subsys(	Pin_Mapping::LED_RED,
									Pin_Mapping::LED_GREEN,
									Pin_Mapping::LED_BLUE,
									Pin_Mapping::EXT_LED_GREEN,
									Pin_Mapping::EXT_LED_YELLOW	);

Comms_Subsys comms_subsys(usb, state_supervisor);

//============== DEBUG UTILITY SETUP ============
Debug_Protobuf dbp(comms_subsys);

//TESTING: TODO, remove
Scheduler print_test_task;


void LINK_SYSTEM_STATE_VARIABLES() {
	//##### MULTICARD INFO #####
	multicard_info_subsys.link_command_sel_input_aux_npic(			state_supervisor.subscribe_multicard_info_sel_input_aux_npic_command()	);
	state_supervisor.link_multicard_info_all_cards_present_status(	multicard_info_subsys.subscribe_status_all_cards_present()				);
	state_supervisor.link_multicard_info_node_id_status(			multicard_info_subsys.subscribe_status_node_id()						);

	//##### ONBOARD POWER MONITOR #####
	pm_onboard_subsys.link_command_regulator_enabled(				state_supervisor.subscribe_pm_onboard_regulator_enable_command()		);
	state_supervisor.link_pm_onboard_debounced_power_status(		pm_onboard_subsys.subscribe_status_debounced_power()					);
	state_supervisor.link_pm_onboard_immediate_power_status(		pm_onboard_subsys.subscribe_status_immedate_power()						);

	//##### MOTHERBOARD POWER MONITOR #####
	pm_motherboard_subsys.link_command_regulator_enabled(			state_supervisor.subscribe_pm_motherboard_regulator_enable_command()	);
	state_supervisor.link_pm_motherboard_debounced_power_status(	pm_motherboard_subsys.subscribe_status_debounced_power()				);
	state_supervisor.link_pm_motherboard_immediate_power_status(	pm_motherboard_subsys.subscribe_status_immedate_power()					);

	//##### ADC OFFSET CONTROL #####
	offset_ctrl_subsys.link_RC_command_offset_dac_read_update(		state_supervisor.subscribe_RC_adc_offset_ctrl_perform_device_read_command()	);
	offset_ctrl_subsys.link_command_offset_dac_values(				state_supervisor.subscribe_adc_offset_ctrl_dac_values_command()				);
	state_supervisor.link_adc_offset_ctrl_dac_value_readback_status(offset_ctrl_subsys.subscribe_status_offset_dac_values_readback()			);
	state_supervisor.link_RC_adc_offset_ctrl_dac_error_status(		offset_ctrl_subsys.subscribe_RC_status_offset_dac_error()					);
	state_supervisor.link_adc_offset_ctrl_device_present_status(	offset_ctrl_subsys.subscribe_status_device_present()						);
	offset_ctrl_subsys.link_status_onboard_pgood(					pm_onboard_subsys.subscribe_status_debounced_power()						);
	//TESTING, TODO: change back to line above
	//offset_ctrl_subsys.link_status_onboard_pgood(dummy_pgood.subscribe());

	//##### HISPEED SUBSYSTEM #####
	hispeed_subsys.link_RC_command_hispeed_SOA_DAC_drive(				state_supervisor.subscribe_RC_command_hispeed_SOA_DAC_drive()			);
	state_supervisor.link_status_hispeed_TIA_ADC_readback(				hispeed_subsys.subscribe_status_hispeed_TIA_ADC_readback()				);
	hispeed_subsys.link_RC_command_hispeed_TIA_enable(					state_supervisor.subscribe_RC_command_hispeed_TIA_enable()				);
	hispeed_subsys.link_RC_command_hispeed_SOA_enable(					state_supervisor.subscribe_RC_command_hispeed_SOA_enable()				);
	hispeed_subsys.link_command_hispeed_arm_fire_request(				state_supervisor.subscribe_RC_command_hispeed_arm_fire_request()		);
	state_supervisor.link_status_hispeed_armed(							hispeed_subsys.subscribe_status_hispeed_armed()							);
	state_supervisor.link_RC_status_hispeed_arm_flag_complete(			hispeed_subsys.subscribe_RC_status_hispeed_arm_flag_complete()			);
	state_supervisor.link_RC_status_hispeed_arm_flag_err_pwr(			hispeed_subsys.subscribe_RC_status_hispeed_arm_flag_err_pwr()			);
	state_supervisor.link_RC_status_hispeed_arm_flag_err_ready(			hispeed_subsys.subscribe_RC_status_hispeed_arm_flag_err_ready()			);
	state_supervisor.link_RC_status_hispeed_arm_flag_err_sync_timeout(	hispeed_subsys.subscribe_RC_status_hispeed_arm_flag_err_sync_timeout()	);
	hispeed_subsys.link_RC_command_hispeed_sdram_load_test_sequence(	state_supervisor.subscribe_RC_command_hispeed_sdram_load_test_sequence());

	//##### CoB TEMP MONITOR #####
	state_supervisor.link_RC_status_cobtemp_temp_sensor_error(			cob_temp_monitor.subscribe_RC_status_temp_sensor_error()				);
	state_supervisor.link_status_cobtemp_cob_temperature_c(				cob_temp_monitor.subscribe_status_cob_temperature_c()					);
	state_supervisor.link_status_cobtemp_device_present(				cob_temp_monitor.subscribe_status_device_present()						);
	state_supervisor.link_status_cobtemp_temp_sensor_device_id(			cob_temp_monitor.subscribe_status_temp_sensor_device_id()				);
	cob_temp_monitor.link_status_onboard_pgood(							pm_onboard_subsys.subscribe_status_debounced_power()					);
	//TESTING, TODO: change back to line above
	//cob_temp_monitor.link_status_onboard_pgood(dummy_pgood.subscribe());

	//##### CoB EEPROM #####
	state_supervisor.link_RC_status_cob_eeprom_write_error(				cob_eeprom.subscribe_RC_status_cob_eeprom_write_error()					);
	state_supervisor.link_status_cob_eeprom_UID(						cob_eeprom.subscribe_status_cob_eeprom_UID()							);
	state_supervisor.link_status_cob_eeprom_contents(					cob_eeprom.subscribe_status_cob_eeprom_contents()						);
	state_supervisor.link_status_cob_eeprom_device_present(				cob_eeprom.subscribe_status_device_present()							);
	cob_eeprom.link_RC_command_cob_eeprom_write(						state_supervisor.subscribe_RC_command_cob_eeprom_write()				);
	cob_eeprom.link_RC_command_cob_eeprom_write_contents(				state_supervisor.subscribe_RC_command_cob_eeprom_write_contents()		);
	cob_eeprom.link_RC_command_cob_eeprom_write_key(					state_supervisor.subscribe_RC_command_cob_eeprom_write_key()			);
	cob_eeprom.link_status_onboard_pgood(								pm_onboard_subsys.subscribe_status_debounced_power()					);
	//TESTING, TODO: change back to the line above
	//cob_eeprom.link_status_onboard_pgood(dummy_pgood.subscribe());

	//##### WAVEGUIDE BIAS DRIVES #####
	state_supervisor.link_status_wgbias_device_present(				wgbias_subsys.subscribe_status_device_present()						);
	state_supervisor.link_status_wgbias_dac_values_readback(		wgbias_subsys.subscribe_status_bias_dac_values_readback()			);
	state_supervisor.link_RC_status_wgbias_dac_error(				wgbias_subsys.subscribe_RC_status_bias_dac_error()					);
	wgbias_subsys.link_RC_command_bias_dac_read_update(				state_supervisor.subscribe_RC_command_wgbias_dac_read_update()		);
	wgbias_subsys.link_RC_command_bias_dac_values(					state_supervisor.subscribe_RC_command_wgbias_dac_values()			);
	wgbias_subsys.link_RC_command_bias_reg_enable(					state_supervisor.subscribe_RC_command_wgbias_reg_enable()			);
	wgbias_subsys.link_status_motherboard_pgood(					pm_motherboard_subsys.subscribe_status_debounced_power()			);

	//##### COMMS INTERFACE #####
	state_supervisor.link_status_comms_connected(					comms_subsys.subscribe_status_comms_connected()						);
	comms_subsys.link_command_comms_allow_connections(				state_supervisor.subscribe_command_comms_allow_connections()		);

	//##### LED INDICATORS ######
	indicators_subsys.link_status_onboard_pgood(		pm_onboard_subsys.subscribe_status_debounced_power()		);
	indicators_subsys.link_status_motherboard_pgood(	pm_motherboard_subsys.subscribe_status_debounced_power()	);
	indicators_subsys.link_RC_status_comms_activity(	comms_subsys.subscribe_RC_status_comms_activity()			);
	indicators_subsys.link_status_comms_connected(		comms_subsys.subscribe_status_comms_connected()				);
	indicators_subsys.link_status_hispeed_armed(		hispeed_subsys.subscribe_status_hispeed_armed()				);
	indicators_subsys.link_status_hispeed_arm_flag_err_pwr(				hispeed_subsys.subscribe_RC_status_hispeed_arm_flag_err_pwr()			);
	indicators_subsys.link_status_hispeed_arm_flag_err_ready(			hispeed_subsys.subscribe_RC_status_hispeed_arm_flag_err_ready()			);
	indicators_subsys.link_status_hispeed_arm_flag_err_sync_timeout(	hispeed_subsys.subscribe_RC_status_hispeed_arm_flag_err_sync_timeout()	);
}

void INIT_SUBSYSTEMS() {
	//init multicard_info_subsys with USB so we get access to the NODE ID
	//multicard_info_subsys.init();
	pm_onboard_subsys.init();
	pm_motherboard_subsys.init();
	offset_ctrl_subsys.init();
	hispeed_subsys.init();
	indicators_subsys.init();
	cob_temp_monitor.init();
	cob_eeprom.init();
	wgbias_subsys.init();
	comms_subsys.init();
}

void ident_node_usb() {
	//init multicard_info to get node ID; build string version of node ID
	multicard_info_subsys.init();
	auto node_id = multicard_info_subsys.subscribe_status_node_id();
	const char lookup[16][9] = {	"_NODE_00", "_NODE_01", "_NODE_02", "_NODE_03", "_NODE_04", "_NODE_05", "_NODE_06", "_NODE_07",
									"_NODE_08", "_NODE_09", "_NODE_10", "_NODE_11", "_NODE_12", "_NODE_13", "_NODE_14", "_NODE_15"	};
	auto str_node = App_String<8>(lookup[node_id.read() & 0x0F]);

	//get the string version of the UID
	auto uid_reader = Board_UID();
	auto str_uid = uid_reader.uid_string();

	//and now make a string wrapper for our serial number
	std::array<uint8_t, 32> node_serial_array;
	std::copy(str_uid.span().begin(), str_uid.span().begin() + 24, node_serial_array.begin());
	std::copy(str_node.span().begin(), str_node.span().end(), node_serial_array.begin() + 24);

	//set our USB serial number accordingly
	usb.set_serial(node_serial_array);
}

void app_init() {
	//link the debug outputs
	Debug::associate(&dbp);

	//then link all subsystems
	LINK_SYSTEM_STATE_VARIABLES();
	Debug::PRINT("STARTED APPLICATION\r\n");

	//initialize all subsystems
	//msc.init();
	//msc.connect_request();
	ident_node_usb();	//enumerate with a serial number according to node ID
	INIT_SUBSYSTEMS();
	Debug::PRINT("SUBSYSTEMS INITIALIZED!\r\n");

	//TESTING, TODO: REMOVE
	print_test_task.schedule_interval_ms([](){Debug::PRINT("Printing Test!\r\n");}, 1000);
}

void app_loop() {
	//just run the scheduler in the main loop--all threads will be running in their respective subsystems
	Scheduler::update();
}
