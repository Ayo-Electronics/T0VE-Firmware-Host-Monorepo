/*
 * app_state_supervisor.cpp
 *
 *  Created on: Aug 18, 2025
 *      Author: govis
 */

#include "app_state_supervisor.hpp"

//protobuf includes
#include "app_messages.pb.h"
#include "pb_common.h"
#include "pb_encode.h"
#include "pb_decode.h"

//and HAL include to reset the system inline
#include "app_hal_reset.hpp"

//static helper for array copy
//have to copy element-wise since types might not match (i.e. uint16 --> uint32)
template<typename T_dest, typename T_src, size_t N>
static void copy_arrays(T_dest (&dest)[N], const std::array<T_src, N>& src) {
	for(size_t i = 0; i < N; i++) dest[i] = src[i];
}

//writing to an array-type state variable
template<typename T_dest, typename T_src, size_t N>
static void write_arrays(std::array<T_dest, N>& dest, const T_src (&src)[N]) {
	for(size_t i = 0; i < N; i++) dest[i] = src[i];			//copy C array into the temp
}

//writing to an array-type state variable
template<typename T_dest, typename T_src, size_t N>
static void write_sv_arrays(Pub_Var<std::array<T_dest, N>>& dest, const T_src (&src)[N]) {
	std::array<T_dest, N> temp_dest = {0};					//create a temporary std::array
	for(size_t i = 0; i < N; i++) temp_dest[i] = src[i];	//copy C array into the temp
	dest.publish(temp_dest);								//atomic write temp into state variable
}

//writing to a string-type state variable

//constructor
//default initialization of all state variables done in header
State_Supervisor::State_Supervisor() {}

//serialization - pb_encode
std::span<uint8_t, std::dynamic_extent> State_Supervisor::serialize() {
	//temporary formatting struct
	app_Communication message = app_Communication_init_zero;

	//say that our message type is gonna be a state update
	message.which_payload = app_Communication_node_state_tag;

	//============================ STATE UPDATES ==============================

	//alias our state, and pop the magic number in
	auto& state = message.payload.node_state;
	state.MAGIC_NUMBER = MAGIC_NUMBER;

	//### STATE SUPERVISOR ###
	state.state_supervisor.status.decode_err = decode_err.read();
	state.state_supervisor.status.decode_err_deserz = decode_err_deserz.read();
	state.state_supervisor.status.decode_err_magic = decode_err_magicn.read();
	state.state_supervisor.status.decode_err_msgtype = decode_err_msgtype.read();
	state.state_supervisor.status.encode_err = encode_err.read();
	state.state_supervisor.status.encode_err_serz = encode_err_serz.read();
	decode_err.write(false);	//resetting atomic flags
	encode_err.write(false); 	//resetting atomic flags

	//#### MULTICARD STATUS ####
	state.multicard.has_command = true;;
	state.multicard.command.has_sel_pd_input_aux_npic = true;
	state.multicard.command.sel_pd_input_aux_npic = multicard_info_sel_input_aux_npic_command.read();
	state.multicard.status.all_present = multicard_info_all_cards_present_status.read();
	state.multicard.status.node_id = multicard_info_node_id_status.read();

	//#### ONBOARD POWER MONITOR ###
	state.pm_onboard.has_command = true;
	state.pm_onboard.command.has_regulator_enable = true;
	state.pm_onboard.command.regulator_enable = pm_onboard_regulator_enable_command.read();
	state.pm_onboard.status.immediate_power_status = pm_onboard_immediate_power_status.read();
	state.pm_onboard.status.debounced_power_status = pm_onboard_debounced_power_status.read();

	//#### MOTHERBOARD POWER MONITOR ####
	state.pm_motherboard.has_command = true;
	state.pm_motherboard.command.has_regulator_enable = true;
	state.pm_motherboard.command.regulator_enable = pm_motherboard_regulator_enable_command.read();
	state.pm_motherboard.status.immediate_power_status = pm_motherboard_immediate_power_status.read();
	state.pm_motherboard.status.debounced_power_status = pm_motherboard_debounced_power_status.read();

	//#### ADC OFFSET CONTROL ####
	state.offset_ctrl.has_command = true;
	state.offset_ctrl.command.has_offset_set = true;
	copy_arrays(state.offset_ctrl.command.offset_set.values, adc_offset_ctrl_dac_values_command.read());
	state.offset_ctrl.command.has_do_readback = true;
	state.offset_ctrl.command.do_readback = adc_offset_ctrl_perform_device_read_command.read(); //will reset when complete
	state.offset_ctrl.status.device_present = adc_offset_ctrl_device_present_status.read();
	state.offset_ctrl.status.device_error = adc_offset_ctrl_dac_error_status.read();
	copy_arrays(state.offset_ctrl.status.offset_readback.values, adc_offset_ctrl_dac_value_readback_status.read());
	adc_offset_ctrl_dac_error_status.acknowledge_reset(); //acknowledge error on read

	//#### HISPEED SUBSYSTEM ####
	state.hispeed.has_command = true;
	state.hispeed.command.has_arm_request = true;
	state.hispeed.command.arm_request = command_hispeed_arm_fire_request.read();
	state.hispeed.command.has_SOA_enable = true;
	copy_arrays(state.hispeed.command.SOA_enable.values, command_hispeed_SOA_enable.read());
	state.hispeed.command.has_TIA_enable = true;
	copy_arrays(state.hispeed.command.TIA_enable.values, command_hispeed_TIA_enable.read());
	state.hispeed.command.has_SOA_DAC_drive = true;
	copy_arrays(state.hispeed.command.SOA_DAC_drive.values, command_hispeed_SOA_DAC_drive.read());
	state.hispeed.status.armed = status_hispeed_armed.read();
	state.hispeed.status.done_err_ready = status_hispeed_arm_flag_err_ready.read();
	state.hispeed.status.done_err_timeout = status_hispeed_arm_flag_err_timeout.read();
	state.hispeed.status.done_err_pwr = status_hispeed_arm_flag_err_pwr.read();
	state.hispeed.status.done_err_cancelled = status_hispeed_arm_flag_err_cancelled.read();
	state.hispeed.status.done_success = status_hispeed_arm_flag_complete.read();
	copy_arrays(state.hispeed.status.tia_adc_readback.values, status_hispeed_TIA_ADC_readback.read());
	//acknowledge read-clear flags
	status_hispeed_arm_flag_complete.acknowledge_reset();
	status_hispeed_arm_flag_err_pwr.acknowledge_reset();
	status_hispeed_arm_flag_err_ready.acknowledge_reset();
	status_hispeed_arm_flag_err_timeout.acknowledge_reset();
	status_hispeed_arm_flag_err_cancelled.acknowledge_reset();

	//#### CoB TEMPERATURE MONITOR ####
	state.cob_temp.status.device_present = status_cobtemp_device_present.read();
	state.cob_temp.status.device_error = status_cobtemp_temp_sensor_error.read();
	state.cob_temp.status.device_id = status_cobtemp_temp_sensor_device_id.read();
	state.cob_temp.status.temperature_celsius = status_cobtemp_cob_temperature_c.read();
	status_cobtemp_temp_sensor_error.acknowledge_reset(); //acknowledge read errors

	//#### CoB EEPROM ####
	state.cob_eeprom.has_command = true;
	state.cob_eeprom.command.has_do_cob_write_desc = true;
	state.cob_eeprom.command.do_cob_write_desc = command_cob_eeprom_write.read();
	state.cob_eeprom.command.has_cob_write_key = true;
	state.cob_eeprom.command.cob_write_key = command_cob_eeprom_write_key.read();
	state.cob_eeprom.command.has_do_cob_write_desc = true;
	copy_arrays(state.cob_eeprom.command.cob_desc_set, command_cob_eeprom_write_contents.read().array());
	state.cob_eeprom.status.device_present = status_cob_eeprom_device_present.read();
	state.cob_eeprom.status.cob_UID = status_cob_eeprom_UID.read();
	state.cob_eeprom.status.device_error = status_cob_eeprom_write_error.read();
	copy_arrays(state.cob_eeprom.status.cob_desc, status_cob_eeprom_contents.read().array());
	status_cob_eeprom_write_error.acknowledge_reset(); //acknowledge write errors

	//#### WAVEGUIDE BIAS DRIVES ####
	state.waveguide_bias.has_command = true;
	state.waveguide_bias.command.has_setpoints = true;
	auto cmd = command_wgbias_dac_values.read();
	copy_arrays(state.waveguide_bias.command.setpoints.bulk_setpoint.values, cmd.bulk_setpoints);
	copy_arrays(state.waveguide_bias.command.setpoints.mid_setpoint.values, cmd.mid_setpoints);
	copy_arrays(state.waveguide_bias.command.setpoints.stub_setpoint.values, cmd.stub_setpoints);
	state.waveguide_bias.command.has_regulator_enable = true;
	state.waveguide_bias.command.regulator_enable = command_wgbias_reg_enable.read();
	state.waveguide_bias.command.has_do_readback = true;
	state.waveguide_bias.command.do_readback = command_wgbias_dac_read_update.read();
	state.waveguide_bias.status.device_present = status_wgbias_device_present.read();
	auto rb = status_wgbias_dac_values_readback.read();	//copy struct element by element
	copy_arrays(state.waveguide_bias.status.setpoints_readback.bulk_setpoint.values, rb.bulk_setpoints);
	copy_arrays(state.waveguide_bias.status.setpoints_readback.mid_setpoint.values, rb.mid_setpoints);
	copy_arrays(state.waveguide_bias.status.setpoints_readback.stub_setpoint.values, rb.stub_setpoints);
	state.waveguide_bias.status.device_error = status_wgbias_dac_error.read();
	status_wgbias_dac_error.acknowledge_reset(); //acknowledge communication errors

	//#### NEURAL MEMORY MANAGER ####
	state.neural_mem_manager.has_command = true;
	state.neural_mem_manager.command.has_check_io_size = true;
	state.neural_mem_manager.command.check_io_size = command_nmemmanager_check_io_size.read();
	state.neural_mem_manager.command.has_load_test_pattern = true;
	state.neural_mem_manager.command.load_test_pattern = command_nmemmanager_load_test_pattern.read();
	state.neural_mem_manager.status.detected_input_size = status_nmemmanager_detected_input_size.read();
	state.neural_mem_manager.status.detected_output_size = status_nmemmanager_detected_output_size.read();
	state.neural_mem_manager.status.mem_attached = status_nmemmanager_mem_attached.read();

	//#### COMMS INTERFACE ####
	state.comms.has_command = true;
	state.comms.command.has_allow_connection = true;
	state.comms.command.allow_connection = command_comms_allow_connections.read();
	state.comms.status.comms_connected = status_comms_connected.read();

	//also report that we aren't performing a system reset
	state.has_do_system_reset = true;
	state.do_system_reset = false;

	//=========================================================================

	//encode the message
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer.data(), encode_buffer.size());
	if(!pb_encode(&stream, app_Communication_fields, &message)) {
		//encoding failed, set our encode failure flag, increment our counter
		encode_err.write(true);
		encode_err_serz++;

		//return an empty span
		return section(encode_buffer, 0, 0);
	}
	else {
		//encode successful, return a view into our encoded buffer
		return section(encode_buffer, 0, stream.bytes_written);
	}
}

//deserialization - pb_decode
void State_Supervisor::deserialize(std::span<uint8_t, std::dynamic_extent> encoded_msg) {
	//temporary to parse into
	app_Communication message = app_Communication_init_zero;

	//create the protobuf input stream type
	pb_istream_t stream = pb_istream_from_buffer(encoded_msg.data(), encoded_msg.size());

	//try to decode the message
	if(!pb_decode(&stream, app_Communication_fields, &message)) {
		decode_err.write(true);
		decode_err_deserz++;
		return;
	}

	//check the message type
	if(message.which_payload != app_Communication_node_state_tag) {
		decode_err.write(true);
		decode_err_msgtype++;	//incorrect message type
		return;
	}

	//and check our magic number
	auto& new_state = message.payload.node_state;
	if(new_state.MAGIC_NUMBER != MAGIC_NUMBER) {
		decode_err.write(true);
		decode_err_magicn++;	//incorrect magic number
		return;
	}

	//everything checks out; update state
	//#### SYSTEM_RESET ####
	//just perform the reset here and now; no need to defer it
	if(new_state.has_do_system_reset) {
		if(new_state.do_system_reset) Reset::do_reset();
	}

	//#### MULTICARD STATUS ####
	if(new_state.multicard.has_command) {
		if(new_state.multicard.command.has_sel_pd_input_aux_npic) {
			multicard_info_sel_input_aux_npic_command.publish(new_state.multicard.command.sel_pd_input_aux_npic);
		}
	}

	//#### ONBOARD POWER MONITOR ###
	//don't allow edits to the power status if we're armed
	if(new_state.pm_onboard.has_command) {
		if(new_state.pm_onboard.command.has_regulator_enable && !status_hispeed_armed.read()) {
			pm_onboard_regulator_enable_command.publish(new_state.pm_onboard.command.regulator_enable);
		}
	}

	//#### MOTHERBOARD POWER MONITOR ####
	//don't allow edits to the power status if we're armed
	if(new_state.pm_motherboard.has_command) {
		if(new_state.pm_motherboard.command.has_regulator_enable && !status_hispeed_armed.read()) {
			pm_motherboard_regulator_enable_command.publish(new_state.pm_motherboard.command.regulator_enable);
		}
	}

	//#### ADC OFFSET CONTROL ####
	if(new_state.offset_ctrl.has_command) {
		if(new_state.offset_ctrl.command.has_offset_set) {
			write_sv_arrays(adc_offset_ctrl_dac_values_command, new_state.offset_ctrl.command.offset_set.values);
		}
		if(new_state.offset_ctrl.command.has_do_readback) {
			adc_offset_ctrl_perform_device_read_command.publish(new_state.offset_ctrl.command.do_readback);
		}
	}

	//#### HISPEED SUBSYSTEM ####
	if(new_state.hispeed.has_command) {
		if(new_state.hispeed.command.has_SOA_DAC_drive) {
			write_sv_arrays(command_hispeed_SOA_DAC_drive, new_state.hispeed.command.SOA_DAC_drive.values);
		}
		if(new_state.hispeed.command.has_SOA_enable) {
			write_sv_arrays(command_hispeed_SOA_enable, new_state.hispeed.command.SOA_enable.values);
		}
		if(new_state.hispeed.command.has_TIA_enable) {
			write_sv_arrays(command_hispeed_TIA_enable, new_state.hispeed.command.TIA_enable.values);
		}
		if(new_state.hispeed.command.has_arm_request) {
			command_hispeed_arm_fire_request.publish(new_state.hispeed.command.arm_request);
		}
	}

	//#### CoB TEMPERATURE MONITOR ####
	//no commands here

	//#### CoB EEPROM ####
	if(new_state.cob_eeprom.has_command) {
		if(new_state.cob_eeprom.command.has_cob_desc_set) {
			//copy array into temporary app string, then assign
			App_String<sizeof(new_state.cob_eeprom.command.cob_desc_set)> temp_desc(new_state.cob_eeprom.command.cob_desc_set);
			command_cob_eeprom_write_contents.publish(temp_desc);
		}
		if(new_state.cob_eeprom.command.has_cob_write_key) {
			command_cob_eeprom_write_key.publish(new_state.cob_eeprom.command.cob_write_key);
		}
		if(new_state.cob_eeprom.command.has_do_cob_write_desc) {
			command_cob_eeprom_write.publish(new_state.cob_eeprom.command.do_cob_write_desc);
		}
	}

	//#### WAVEGUIDE BIAS DRIVES ####
	if(new_state.waveguide_bias.has_command) {
		if(new_state.waveguide_bias.command.has_do_readback) {
			command_wgbias_dac_read_update.publish(new_state.waveguide_bias.command.do_readback);
		}
		if(new_state.waveguide_bias.command.has_regulator_enable) {
			command_wgbias_reg_enable.publish(new_state.waveguide_bias.command.regulator_enable);
		}
		if(new_state.waveguide_bias.command.has_setpoints) {
			Waveguide_Bias_Drive::Waveguide_Bias_Setpoints_t sp = {0};											//temporary to copy setpoints into
			write_arrays(sp.stub_setpoints, new_state.waveguide_bias.command.setpoints.stub_setpoint.values);	//copy stubs into temp
			write_arrays(sp.mid_setpoints, new_state.waveguide_bias.command.setpoints.mid_setpoint.values);		//copy mids into temp
			write_arrays(sp.bulk_setpoints, new_state.waveguide_bias.command.setpoints.bulk_setpoint.values);	//copy bulks into temp
			command_wgbias_dac_values.publish(sp);	//copy the temp into the state variable
		}
	}

	//#### NEURAL MEMORY MANAGER ####
	if(new_state.neural_mem_manager.has_command) {
		if(new_state.neural_mem_manager.command.has_check_io_size) {
			command_nmemmanager_check_io_size.publish(new_state.neural_mem_manager.command.check_io_size);
		}
		if(new_state.neural_mem_manager.command.has_load_test_pattern) {
			command_nmemmanager_load_test_pattern.publish(new_state.neural_mem_manager.command.load_test_pattern);
		}
	}

	//#### COMMS INTERFACE ####
	if(new_state.comms.has_command) {
		if(new_state.comms.command.has_allow_connection) {
			command_comms_allow_connections.publish(new_state.comms.command.allow_connection);
		}
	}
}






