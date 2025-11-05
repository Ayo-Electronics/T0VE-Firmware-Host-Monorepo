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

#include "app_threading.hpp" 		//so we have references to the read/write ports
#include "app_string.hpp"			//for CoB eeprom reading/writing

#include "app_cob_eeprom.hpp"		//for CoB memory size
#include "app_bias_drives.hpp"		//for waveguide bias setpoint struct

class State_Supervisor {
public:
	//constructor--don't strictly need to do anything here, gonna default construct all the state variables and subscription channels
	State_Supervisor();

	//init function sets up a monitoring thread that checks connection status
	//turns off power when we disconnect; lets rest of subsystems handle putting state variables in safe states
	//TODO: this, especially how to handle USB suspension during high-speed execution

	//for encode and decode, pop in a magic number to "sign" state as valid
	//if decode doesn't match this magic number, don't perform state updates
	static constexpr uint32_t MAGIC_NUMBER = 0xA5A5A5A5;

	//serialization/deserialization of state messages
	//should be ISR safe, but try not to run from there
	std::span<uint8_t, std::dynamic_extent> serialize();
	void deserialize(std::span<uint8_t, std::dynamic_extent> encoded_msg);

	//============================== FUNCTIONS TO LINK SUBSCRIPTIONS ==============================
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
	LINK_FUNC_RC(		status_hispeed_arm_flag_err_timeout			);
	LINK_FUNC_RC(		status_hispeed_arm_flag_err_pwr				);
	LINK_FUNC_RC(		status_hispeed_arm_flag_err_cancelled		);
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
	LINK_FUNC_RC(		status_wgbias_dac_error						);
	SUBSCRIBE_FUNC_RC(	command_wgbias_dac_values					);
	SUBSCRIBE_FUNC_RC(	command_wgbias_reg_enable					);
	SUBSCRIBE_FUNC_RC(	command_wgbias_dac_read_update				);

	//#### NEURAL MEMORY MANAGER ####
	LINK_FUNC(status_nmemmanager_detected_input_size);
	LINK_FUNC(status_nmemmanager_detected_output_size);
	SUBSCRIBE_FUNC_RC(command_nmemmanager_check_io_size);
	SUBSCRIBE_FUNC_RC(command_nmemmanager_load_test_pattern);
	LINK_FUNC(status_nmemmanager_mem_attached);

	//#### COMMS INTERFACE ####
	LINK_FUNC(			status_comms_connected						);
	SUBSCRIBE_FUNC(		command_comms_allow_connections				);

private:
	//special atomic variables to see if we decoded our most recent protobuf message successfully
	Atomic_Var<bool> decode_err = false;
	Atomic_Var<size_t> decode_err_deserz = 0;	//error when deserializing
	Atomic_Var<size_t> decode_err_magicn = 0;	//error due to incorrect magic number
	Atomic_Var<size_t> decode_err_msgtype = 0;	//error due to incorrect message type
	Atomic_Var<bool> encode_err = false;
	Atomic_Var<size_t> encode_err_serz = 0;		//error due to serialization

	//and a buffer to dump our most recently encoded data
	static constexpr size_t ENCODE_BUFFER_SIZE = 2048;
	std::array<uint8_t, ENCODE_BUFFER_SIZE> encode_buffer;

	//#### MULTICARD INFORMATION ####
	Sub_Var<bool> multicard_info_all_cards_present_status; 				//true if all cards are present
	Sub_Var<uint8_t> multicard_info_node_id_status; 					//node ID of the particular card
	PERSISTENT((Pub_Var<bool>), multicard_info_sel_input_aux_npic_command);	//true if our final layer is from the aux inputs, false if it's from the PIC inputs

	//#### ONBOARD POWER MONITOR STATES ####
	Sub_Var<bool> pm_onboard_immediate_power_status;
	Sub_Var<bool> pm_onboard_debounced_power_status;
	PERSISTENT((Pub_Var<bool>), pm_onboard_regulator_enable_command, true);	//enables local regulators if power present

	//#### MOTHERBOARD POWER MONITOR STATES ####
	Sub_Var<bool> pm_motherboard_immediate_power_status;
	Sub_Var<bool> pm_motherboard_debounced_power_status;
	PERSISTENT((Pub_Var<bool>), pm_motherboard_regulator_enable_command, true); //TODO: REVERT TO FALSE AFTER TESTING

	//#### ADC OFFSET CONTROL STATES ####
	Sub_Var<bool> adc_offset_ctrl_device_present_status; 						//report whether we detected the device during initialization
	Sub_Var_RC<bool> adc_offset_ctrl_dac_error_status;							//whether there's been some kinda error with the offset DAC, read clear
	Sub_Var<std::array<uint16_t, 4>> adc_offset_ctrl_dac_value_readback_status; //current DAC values written to the ADC offset control system
	PERSISTENT((Pub_Var<std::array<uint16_t, 4>>), adc_offset_ctrl_dac_values_command); //values we want to command to the ADC offset DAC
	PERSISTENT((Pub_Var<bool>), adc_offset_ctrl_perform_device_read_command); 		//assert this flag when we want to perform a device read, cleared upon service

	//#### HISPEED SUBSYSTEM #####
	PERSISTENT((Pub_Var<bool>), command_hispeed_arm_fire_request);
	Sub_Var<bool>			status_hispeed_armed;
	Sub_Var_RC<bool>		status_hispeed_arm_flag_err_ready;
	Sub_Var_RC<bool>		status_hispeed_arm_flag_err_timeout;
	Sub_Var_RC<bool>		status_hispeed_arm_flag_err_pwr;
	Sub_Var_RC<bool>		status_hispeed_arm_flag_err_cancelled;
	Sub_Var_RC<bool>		status_hispeed_arm_flag_complete;
	PERSISTENT((Pub_Var<bool>), command_hispeed_sdram_load_test_sequence);
	PERSISTENT((Pub_Var<std::array<bool, 4>>), command_hispeed_SOA_enable);
	PERSISTENT((Pub_Var<std::array<bool, 4>>), command_hispeed_TIA_enable);
	PERSISTENT((Pub_Var<std::array<uint16_t, 4>>), command_hispeed_SOA_DAC_drive);
	Sub_Var<std::array<uint16_t, 4>>	status_hispeed_TIA_ADC_readback;

	//#### CoB TEMPERATURE MONITOR ####
	Sub_Var<bool> status_cobtemp_device_present; 			//report whether we detected the device during initialization
	Sub_Var_RC<bool> status_cobtemp_temp_sensor_error; 		//asserted when any kinda temperature sensor error happens
	Sub_Var<uint16_t> status_cobtemp_temp_sensor_device_id; //report the device ID of the detected temperature sensing
	Sub_Var<float> status_cobtemp_cob_temperature_c;		//actual temperature reported by the sensor in deg C

	//#### CoB EEPROM #####
	Sub_Var<bool> status_cob_eeprom_device_present;	//report whether we detected the CoB EEPROM device during initialization
	Sub_Var<uint32_t> status_cob_eeprom_UID;		//report the UID reported by the EEPROM connected to the CoB
	Sub_Var_RC<bool> status_cob_eeprom_write_error;	//report any issues that happened during eeprom write
	Sub_Var<App_String<EEPROM_24AA02UID::MEMORY_SIZE_BYTES>> status_cob_eeprom_contents; 		//user RW section of the eeprom
	PERSISTENT((Pub_Var<App_String<EEPROM_24AA02UID::MEMORY_SIZE_BYTES>>), command_cob_eeprom_write_contents); //what to write to the eeprom
	PERSISTENT((Pub_Var<bool>), command_cob_eeprom_write);			//command to write to the CoB EEPROM
	PERSISTENT((Pub_Var<uint32_t>), command_cob_eeprom_write_key); //only write to the EEPROM if the keys match

	//#### WAVEGUIDE BIAS DRIVES #####
	Sub_Var<bool> status_wgbias_device_present;			//report whether we detected the device during initialization
	Sub_Var<Waveguide_Bias_Drive::Waveguide_Bias_Setpoints_t> status_wgbias_dac_values_readback;	//what the DAC thinks we've written to it
	Sub_Var_RC<bool> status_wgbias_dac_error;			//asserted when any kinda dac error happens, expose read-clear port
	PERSISTENT((Pub_Var<Waveguide_Bias_Drive::Waveguide_Bias_Setpoints_t>), command_wgbias_dac_values);			//values we'd like to write to the DAC
	PERSISTENT((Pub_Var<bool>), command_wgbias_reg_enable);			//enable the actual voltage regulators for the waveguide bias system
	PERSISTENT((Pub_Var<bool>), command_wgbias_dac_read_update);		//asserted when we want to perform a DAC read, cleared after read

	//#### NEURAL MEMORY MANAGER ####
	Sub_Var<uint32_t> status_nmemmanager_detected_input_size;				//reports how many valid network inputs we've detected
	Sub_Var<uint32_t> status_nmemmanager_detected_output_size;				//report how many valid network outputs we've detected
	PERSISTENT((Pub_Var<bool>), command_nmemmanager_check_io_size);			//ask the memory manager to try detecting the input/output size
	PERSISTENT((Pub_Var<uint32_t>), command_nmemmanager_load_test_pattern); //set this to a non-zero value to load a test pattern into DRAM
	Sub_Var<bool> status_nmemmanager_mem_attached;							//reports whether the memory is being exposed over the MSC interface

	//#### COMMS INTERFACE ####
	Sub_Var<bool> status_comms_connected;					//report whether we're connected to a host
	PERSISTENT((Pub_Var<bool>), command_comms_allow_connections);	//allow connections to a host
};
