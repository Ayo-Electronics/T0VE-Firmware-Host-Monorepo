/*
 * app_state_supervisor.hpp
 *
 *  Created on: Aug 18, 2025
 *      Author: govis
 *
 *  The idea here is mostly to have some kinda aggregation of system-wide state
 *  We'll basically attach a bunch of state variables here which should allow reading/writing of state of all subsystems
 *  Will also include the protobuf encoders and decoders here, coz it seems like a logical place to do so
 *
 *  NOTE:
 *  	\--> regarding the command decode, it should be OKAY to do this in low-priority ISR context
 *  	\--> updating state variables are simply writing to variables atomically and setting some flags
 *  	\--> actual subsystem threads will bear the brunt work of executing on state changes
 *  		\--> subsystem threads will run in non-ISR contexts typically
 */

#pragma once

#include "app_state_variable.hpp" 	//so we have references to the read/write ports

#include "app_cob_eeprom.hpp"		//for CoB memory size
#include "app_bias_drives.hpp"		//for waveguide bias setpoint struct

class State_Supervisor {
public:
	//constructor--don't strictly need to do anything here, gonna default construct all the state variables and subscription channels
	State_Supervisor();

	//serialization/deserialization of state messages
	//MAY RUN FROM ISR CONTEXT
	void serialize(); //TODO: nanoPB encode
	void deserialize(); //TODO: nanoPB decode

	//============================== FUNCTIONS TO LINK SUBSCRIPTIONS ==============================
	//#### STATE SUPERVISOR #####
	SUBSCRIBE_FUNC_RC(	notify_comms_activity	);

	//#### MULTICARD INFORMATION ####
	LINK_FUNC(			multicard_info_all_cards_present_status		);
	LINK_FUNC(			multicard_info_node_id_status				);
	SUBSCRIBE_FUNC(		multicard_info_sel_input_aux_npic_command	);

	//#### ONBOARD POWER MONITOR STATES ####
	LINK_FUNC(			pm_onboard_immediate_power_status			);
	LINK_FUNC(			pm_onboard_debounced_power_status			);
	SUBSCRIBE_FUNC(		pm_onboard_regulator_enable_command			);

	//#### MOTHERBOARD POWER MONITOR STATES ####
	LINK_FUNC(			pm_motherboard_immediate_power_status		);
	LINK_FUNC(			pm_motherboard_debounced_power_status		);
	SUBSCRIBE_FUNC(		pm_motherboard_regulator_enable_command		);

	//#### ADC OFFSET CONTROL STATES ####
	LINK_FUNC(			adc_offset_ctrl_device_present_status		);
	LINK_FUNC_RC(		adc_offset_ctrl_dac_error_status			);
	LINK_FUNC(			adc_offset_ctrl_dac_value_readback_status	);
	SUBSCRIBE_FUNC(		adc_offset_ctrl_dac_values_command			);
	SUBSCRIBE_FUNC_RC(	adc_offset_ctrl_perform_device_read_command	);

	//#### HISPEED SUBSYSTEM #####
	SUBSCRIBE_FUNC_RC(	command_hispeed_arm_fire_request			);
	LINK_FUNC(			status_hispeed_armed						);
	LINK_FUNC_RC(		status_hispeed_arm_flag_err_ready			);
	LINK_FUNC_RC(		status_hispeed_arm_flag_err_sync_timeout	);
	LINK_FUNC_RC(		status_hispeed_arm_flag_err_pwr				);
	LINK_FUNC_RC(		status_hispeed_arm_flag_complete			);
	SUBSCRIBE_FUNC_RC(	command_hispeed_sdram_load_test_sequence	);
	SUBSCRIBE_FUNC_RC(	command_hispeed_SOA_enable					);
	SUBSCRIBE_FUNC_RC(	command_hispeed_TIA_enable					);
	SUBSCRIBE_FUNC_RC(	command_hispeed_SOA_DAC_drive				);
	LINK_FUNC(			status_hispeed_TIA_ADC_readback				);

	//#### CoB TEMPERATURE MONITOR ####
	LINK_FUNC(			status_cobtemp_device_present				);
	LINK_FUNC_RC(		status_cobtemp_temp_sensor_error			);
	LINK_FUNC(			status_cobtemp_temp_sensor_device_id		);
	LINK_FUNC(			status_cobtemp_cob_temperature_c			);

	//#### CoB EEPROM ####
	LINK_FUNC(			status_cob_eeprom_device_present			);
	LINK_FUNC(			status_cob_eeprom_UID						);
	LINK_FUNC(			status_cob_eeprom_contents					);
	LINK_FUNC_RC(		status_cob_eeprom_write_error				);
	SUBSCRIBE_FUNC_RC(	command_cob_eeprom_write					);
	SUBSCRIBE_FUNC_RC(	command_cob_eeprom_write_key				);
	SUBSCRIBE_FUNC_RC(	command_cob_eeprom_write_contents			);

	//#### WAVEGUIDE BIAS DRIVES ####
	LINK_FUNC(			status_wgbias_device_present				);
	LINK_FUNC(			status_wgbias_dac_values_readback			);
	LINK_FUNC_RC(		status_wgbias_dac_error					);
	SUBSCRIBE_FUNC_RC(	command_wgbias_dac_values					);
	SUBSCRIBE_FUNC_RC(	command_wgbias_reg_enable					);
	SUBSCRIBE_FUNC_RC(	command_wgbias_dac_read_update				);

private:
	//#### PRIVATE VARIABLES FOR STATE SUPERVISOR #####
	State_Variable<bool> notify_comms_activity = {false};

	//#### MULTICARD INFORMATION ####
	SV_Subscription<bool> multicard_info_all_cards_present_status; //true if all cards are present
	SV_Subscription<uint8_t> multicard_info_node_id_status; //node ID of the particular card
	State_Variable<bool> multicard_info_sel_input_aux_npic_command = {false}; //true if our final layer is from the aux inputs, false if it's from the PIC inputs

	//#### ONBOARD POWER MONITOR STATES ####
	SV_Subscription<bool> pm_onboard_immediate_power_status;
	SV_Subscription<bool> pm_onboard_debounced_power_status;
	State_Variable<bool> pm_onboard_regulator_enable_command = {true};

	//#### MOTHERBOARD POWER MONITOR STATES ####
	SV_Subscription<bool> pm_motherboard_immediate_power_status;
	SV_Subscription<bool> pm_motherboard_debounced_power_status;
	State_Variable<bool> pm_motherboard_regulator_enable_command = {true}; //TODO: REVERT TO FALSE AFTER TESTING

	//#### ADC OFFSET CONTROL STATES ####
	SV_Subscription<bool> adc_offset_ctrl_device_present_status; //report whether we detected the device during initialization
	SV_Subscription_RC<bool> adc_offset_ctrl_dac_error_status; //whether there's been some kinda error with the offset DAC, read clear
	SV_Subscription<std::array<uint16_t, 4>> adc_offset_ctrl_dac_value_readback_status; //current DAC values written to the ADC offset control system
	State_Variable<std::array<uint16_t, 4>> adc_offset_ctrl_dac_values_command = {{2000, 2000, 2000, 2000}}; //values we want to command to the ADC offset DAC
	State_Variable<bool> adc_offset_ctrl_perform_device_read_command = {true}; //assert this flag when we want to perform a device read, cleared upon service

	//#### HISPEED SUBSYSTEM #####
	State_Variable<bool> 			command_hispeed_arm_fire_request = {false};
	SV_Subscription<bool>			status_hispeed_armed;
	SV_Subscription_RC<bool>		status_hispeed_arm_flag_err_ready;
	SV_Subscription_RC<bool>		status_hispeed_arm_flag_err_sync_timeout;
	SV_Subscription_RC<bool>		status_hispeed_arm_flag_err_pwr;
	SV_Subscription_RC<bool>		status_hispeed_arm_flag_complete;
	State_Variable<bool> 			command_hispeed_sdram_load_test_sequence = {false};
	State_Variable<std::array<bool, 4>> 		command_hispeed_SOA_enable = {{false, false, false, false}};
	State_Variable<std::array<bool, 4>> 		command_hispeed_TIA_enable = {{false, false, false, false}};
	State_Variable<std::array<uint16_t, 4>> 	command_hispeed_SOA_DAC_drive = {{0, 0, 0, 0}};
	SV_Subscription<std::array<uint16_t, 4>>	status_hispeed_TIA_ADC_readback;

	//#### CoB TEMPERATURE MONITOR ####
	SV_Subscription<bool> status_cobtemp_device_present; //report whether we detected the device during initialization
	SV_Subscription_RC<bool> status_cobtemp_temp_sensor_error; //asserted when any kinda temperature sensor error happens
	SV_Subscription<uint16_t> status_cobtemp_temp_sensor_device_id; //report the device ID of the detected temperature sensing
	SV_Subscription<float> status_cobtemp_cob_temperature_c; //actual temperature reported by the sensor in deg C

	//#### CoB EEPROM #####
	SV_Subscription<bool> status_cob_eeprom_device_present;	//report whether we detected the CoB EEPROM device during initialization
	SV_Subscription<uint32_t> status_cob_eeprom_UID;		//report the UID reported by the EEPROM connected to the CoB
	SV_Subscription_RC<bool> status_cob_eeprom_write_error;	//report any issues that happened during eeprom write
	SV_Subscription<std::array<uint8_t, EEPROM_24AA02UID::MEMORY_SIZE_BYTES>> status_cob_eeprom_contents; //user RW section of the eeprom
	State_Variable<std::array<uint8_t, EEPROM_24AA02UID::MEMORY_SIZE_BYTES>> command_cob_eeprom_write_contents; //what to write to the eeprom
	State_Variable<bool> command_cob_eeprom_write;			//command to write to the CoB EEPROM
	State_Variable<uint32_t> command_cob_eeprom_write_key; 	//only write to the EEPROM if the keys match

	//#### WAVEGUIDE BIAS DRIVES #####
	SV_Subscription<bool> status_wgbias_device_present;			//report whether we detected the device during initialization
	SV_Subscription<Waveguide_Bias_Drive::Waveguide_Bias_Setpoints_t> status_wgbias_dac_values_readback;	//what the DAC thinks we've written to it
	SV_Subscription_RC<bool> status_wgbias_dac_error;			//asserted when any kinda dac error happens, expose read-clear port
	State_Variable<Waveguide_Bias_Drive::Waveguide_Bias_Setpoints_t> command_wgbias_dac_values;				//values we'd like to write to the DAC
	State_Variable<bool> command_wgbias_reg_enable;				//enable the actual voltage regulators for the waveguide bias system
	State_Variable<bool> command_wgbias_dac_read_update;		//asserted when we want to perform a DAC read, cleared after read
};
