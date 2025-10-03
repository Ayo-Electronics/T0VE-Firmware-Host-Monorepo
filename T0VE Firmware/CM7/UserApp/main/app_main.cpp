/*
 * app_main.cpp
 *
 *  Created on: Sep 12, 2023
 *      Author: Ishaan
 */

#include <app_hal_pwm.hpp>
#include <app_hal_tick.hpp>
#include "app_main.hpp"

//========== DEBUG INCLUDES ==========
#include "app_debug_vcp.hpp"

//========== HAL INCLUDES ==========
#include "app_hal_gpio.hpp"
#include "app_hal_pin_mapping.hpp"
#include "app_hal_i2c.hpp"
#include "app_hal_spi.hpp"
#include "app_hal_dram.hpp"
#include "app_hal_pwm.hpp"

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

//========= UTILITY INCLUDES =========
#include "app_scheduler.hpp"

//============= INSTANTIATION OF HARDWARE ============
Aux_I2C i2c_bus(Aux_I2C::AUX_I2C_HARDWARE);
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

//=============== CREATING SYSTEM STATE AGGREGATION + LINKING SUBSYSTEM STATE VARIABLES ===============
State_Supervisor state_supervisor;

//TESTING: TODO, remove

void LINK_SYSTEM_STATE_VARIABLES() {
	//##### MULTICARD INFO #####
	multicard_info_subsys.link_command_sel_input_aux_npic(			state_supervisor.subscribe_multicard_info_sel_input_aux_npic_command()	);
	state_supervisor.link_multicard_info_all_cards_present_status(	multicard_info_subsys.subscribe_status_all_cards_present()				);
	state_supervisor.link_multicard_info_node_id_status(			multicard_info_subsys.subscribe_status_node_id()						);

	//##### ONBOARD POWER MONITOR #####
	pm_onboard_subsys.link_command_regulator_enabled(				state_supervisor.subscribe_pm_onboard_regulator_enable_command()		);
	state_supervisor.link_pm_onboard_debounced_power_status(		pm_onboard_subsys.subscribe_debounced_power_status()					);
	state_supervisor.link_pm_onboard_immediate_power_status(		pm_onboard_subsys.subscribe_immediate_power_status()					);

	//##### MOTHERBOARD POWER MONITOR #####
	pm_motherboard_subsys.link_command_regulator_enabled(			state_supervisor.subscribe_pm_motherboard_regulator_enable_command()	);
	state_supervisor.link_pm_motherboard_debounced_power_status(	pm_motherboard_subsys.subscribe_debounced_power_status()				);
	state_supervisor.link_pm_motherboard_immediate_power_status(	pm_motherboard_subsys.subscribe_immediate_power_status()				);

	//##### ADC OFFSET CONTROL #####
	offset_ctrl_subsys.link_RC_command_offset_dac_read_update(		state_supervisor.subscribe_RC_adc_offset_ctrl_perform_device_read_command()	);
	offset_ctrl_subsys.link_command_offset_dac_values(				state_supervisor.subscribe_adc_offset_ctrl_dac_values_command()				);
	state_supervisor.link_adc_offset_ctrl_dac_value_readback_status(offset_ctrl_subsys.subscribe_status_offset_dac_values_readback()			);
	state_supervisor.link_RC_adc_offset_ctrl_dac_error_status(		offset_ctrl_subsys.subscribe_RC_status_offset_dac_error()					);
	state_supervisor.link_adc_offset_ctrl_device_present_status(	offset_ctrl_subsys.subscribe_status_device_present()						);
	offset_ctrl_subsys.link_status_onboard_pgood(					pm_onboard_subsys.subscribe_debounced_power_status()						);
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

	//##### LED INDICATORS ######
	indicators_subsys.link_RC_status_comms_activity(	state_supervisor.subscribe_RC_notify_comms_activity()	);
	indicators_subsys.link_status_hispeed_armed(		hispeed_subsys.subscribe_status_hispeed_armed()			);
	indicators_subsys.link_status_onboard_pgood(		pm_onboard_subsys.subscribe_debounced_power_status()	);

	//##### CoB TEMP MONITOR #####
	state_supervisor.link_RC_status_cobtemp_temp_sensor_error(			cob_temp_monitor.subscribe_RC_status_temp_sensor_error()				);
	state_supervisor.link_status_cobtemp_cob_temperature_c(				cob_temp_monitor.subscribe_status_cob_temperature_c()					);
	state_supervisor.link_status_cobtemp_device_present(				cob_temp_monitor.subscribe_status_device_present()						);
	state_supervisor.link_status_cobtemp_temp_sensor_device_id(			cob_temp_monitor.subscribe_status_temp_sensor_device_id()				);
	cob_temp_monitor.link_status_onboard_pgood(							pm_onboard_subsys.subscribe_debounced_power_status()					);
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
	cob_eeprom.link_status_onboard_pgood(								pm_onboard_subsys.subscribe_debounced_power_status()					);
	//TESTING, TODO: change back to the line above
	//cob_eeprom.link_status_onboard_pgood(dummy_pgood.subscribe());

	//##### WAVEGUIDE BIAS DRIVES #####
	state_supervisor.link_status_wgbias_device_present(				wgbias_subsys.subscribe_status_device_present()						);
	state_supervisor.link_status_wgbias_dac_values_readback(		wgbias_subsys.subscribe_status_bias_dac_values_readback()			);
	state_supervisor.link_RC_status_wgbias_dac_error(				wgbias_subsys.subscribe_RC_status_bias_dac_error()					);
	wgbias_subsys.link_RC_command_bias_dac_read_update(				state_supervisor.subscribe_RC_command_wgbias_dac_read_update()		);
	wgbias_subsys.link_RC_command_bias_dac_values(					state_supervisor.subscribe_RC_command_wgbias_dac_values()			);
	wgbias_subsys.link_RC_command_bias_reg_enable(					state_supervisor.subscribe_RC_command_wgbias_reg_enable()			);
	wgbias_subsys.link_status_motherboard_pgood(					pm_motherboard_subsys.subscribe_debounced_power_status()			);
}

void INIT_SUBSYSTEMS() {
	multicard_info_subsys.init();
	pm_onboard_subsys.init();
	pm_motherboard_subsys.init();
	offset_ctrl_subsys.init();
	hispeed_subsys.init();
	indicators_subsys.init();
	cob_temp_monitor.init();
	cob_eeprom.init();
	wgbias_subsys.init();
}

void app_init() {
	//start by linking all subsystems
	LINK_SYSTEM_STATE_VARIABLES();

	VCP_Debug::init();
	Tick::delay_ms(5000); //get the USB connection initialized

	VCP_Debug::print("STARTED APPLICATION\r\n");

	//initialize all subsystems
	INIT_SUBSYSTEMS();

	VCP_Debug::print("SUBSYSTEMS INITIALIZED!\r\n");

	//TESTING, TODO: REMOVE
}

void app_loop() {
	//just run the scheduler in the main loop--all threads will be running in their respective subsystems
	Scheduler::update();
}
