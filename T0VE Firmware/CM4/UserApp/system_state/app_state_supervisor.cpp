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
	App_Communication message = App_Communication_init_zero;

	//say that our message type is gonna be a state update
	message.which_payload = App_Communication_node_state_tag;

	//============================ STATE UPDATES ==============================

	//alias our state, and pop the magic number in
	auto& state = message.payload.node_state;
	state.MAGIC_NUMBER = MAGIC_NUMBER;

	//### STATE SUPERVISOR ###
	state.state_supervisor_st.decode_err = decode_err.read();
	state.state_supervisor_st.decode_err_deserz = decode_err_deserz.read();
	state.state_supervisor_st.decode_err_magic = decode_err_magicn.read();
	state.state_supervisor_st.decode_err_msgtype = decode_err_msgtype.read();
	state.state_supervisor_st.encode_err = encode_err.read();
	state.state_supervisor_st.encode_err_serz = encode_err_serz.read();
	decode_err.write(false);	//resetting atomic flags
	encode_err.write(false); //resetting atomic flags

	//#### MULTICARD STATUS ####
	state.multicard_cmd.has_sel_pd_input_aux_npic = true;
	state.multicard_cmd.sel_pd_input_aux_npic = multicard_info_sel_input_aux_npic_command.read();
	state.multicard_st.all_present = multicard_info_all_cards_present_status.read();
	state.multicard_st.node_id = multicard_info_node_id_status.read();

	//#### ONBOARD POWER MONITOR ###
	state.pm_onboard_cmd.has_regulator_enable = true;
	state.pm_onboard_cmd.regulator_enable = pm_onboard_regulator_enable_command.read();
	state.pm_onboard_st.immediate_power_status = pm_onboard_immediate_power_status.read();
	state.pm_onboard_st.debounced_power_status = pm_onboard_debounced_power_status.read();

	//#### MOTHERBOARD POWER MONITOR ####
	state.pm_motherboard_cmd.has_regulator_enable = true;
	state.pm_motherboard_cmd.regulator_enable = pm_motherboard_regulator_enable_command.read();
	state.pm_motherboard_st.immediate_power_status = pm_motherboard_immediate_power_status.read();
	state.pm_motherboard_st.debounced_power_status = pm_motherboard_debounced_power_status.read();

	//#### ADC OFFSET CONTROL ####
	state.offset_ctrl_cmd.has_offset_set = true;
	copy_arrays(state.offset_ctrl_cmd.offset_set.values, adc_offset_ctrl_dac_values_command.read());
	state.offset_ctrl_cmd.has_do_readback = true;
	state.offset_ctrl_cmd.do_readback = adc_offset_ctrl_perform_device_read_command.read(); //will reset when complete
	state.offset_ctrl_st.device_present = adc_offset_ctrl_device_present_status.read();
	state.offset_ctrl_st.device_error = adc_offset_ctrl_dac_error_status.read();
	copy_arrays(state.offset_ctrl_st.offset_readback.values, adc_offset_ctrl_dac_value_readback_status.read());
	adc_offset_ctrl_dac_error_status.acknowledge_reset(); //acknowledge error on read

	//#### HISPEED SUBSYSTEM ####
	state.hispeed_cmd.has_arm_request = true;
	state.hispeed_cmd.arm_request = command_hispeed_arm_fire_request.read();
	state.hispeed_cmd.has_load_test_sequence = true;
	state.hispeed_cmd.load_test_sequence = command_hispeed_sdram_load_test_sequence.read();
	state.hispeed_cmd.has_SOA_enable = true;
	copy_arrays(state.hispeed_cmd.SOA_enable.values, command_hispeed_SOA_enable.read());
	state.hispeed_cmd.has_TIA_enable = true;
	copy_arrays(state.hispeed_cmd.TIA_enable.values, command_hispeed_TIA_enable.read());
	state.hispeed_cmd.has_SOA_DAC_drive = true;
	copy_arrays(state.hispeed_cmd.SOA_DAC_drive.values, command_hispeed_SOA_DAC_drive.read());
	state.hispeed_st.armed = status_hispeed_armed.read();
	state.hispeed_st.done_err_ready = status_hispeed_arm_flag_err_ready.read();
	state.hispeed_st.done_err_timeout = status_hispeed_arm_flag_err_sync_timeout.read();
	state.hispeed_st.done_err_pwr = status_hispeed_arm_flag_err_pwr.read();
	state.hispeed_st.done_success = status_hispeed_arm_flag_complete.read();
	copy_arrays(state.hispeed_st.tia_adc_readback.values, status_hispeed_TIA_ADC_readback.read());
	//acknowledge read-clear flags
	status_hispeed_arm_flag_complete.acknowledge_reset();
	status_hispeed_arm_flag_err_pwr.acknowledge_reset();
	status_hispeed_arm_flag_err_ready.acknowledge_reset();
	status_hispeed_arm_flag_err_sync_timeout.acknowledge_reset();

	//#### CoB TEMPERATURE MONITOR ####
	state.cob_temp_st.device_present = status_cobtemp_device_present.read();
	state.cob_temp_st.device_error = status_cobtemp_temp_sensor_error.read();
	state.cob_temp_st.device_id = status_cobtemp_temp_sensor_device_id.read();
	state.cob_temp_st.temperature_celsius = status_cobtemp_cob_temperature_c.read();
	status_cobtemp_temp_sensor_error.acknowledge_reset(); //acknowledge read errors

	//#### CoB EEPROM ####
	state.cob_eeprom_cmd.has_do_cob_write_desc = true;
	state.cob_eeprom_cmd.do_cob_write_desc = command_cob_eeprom_write.read();
	state.cob_eeprom_cmd.has_cob_write_key = true;
	state.cob_eeprom_cmd.cob_write_key = command_cob_eeprom_write_key.read();
	state.cob_eeprom_cmd.has_do_cob_write_desc = true;
	copy_arrays(state.cob_eeprom_cmd.cob_desc_set, command_cob_eeprom_write_contents.read().array());
	state.cob_eeprom_st.device_present = status_cob_eeprom_device_present.read();
	state.cob_eeprom_st.cob_UID = status_cob_eeprom_UID.read();
	state.cob_eeprom_st.device_error = status_cob_eeprom_write_error.read();
	copy_arrays(state.cob_eeprom_st.cob_desc, status_cob_eeprom_contents.read().array());
	status_cob_eeprom_write_error.acknowledge_reset(); //acknowledge write errors

	//#### WAVEGUIDE BIAS DRIVES ####
	state.wg_bias_cmd.has_setpoints = true;
	auto cmd = command_wgbias_dac_values.read();
	copy_arrays(state.wg_bias_cmd.setpoints.bulk_setpoint.values, cmd.bulk_setpoints);
	copy_arrays(state.wg_bias_cmd.setpoints.mid_setpoint.values, cmd.mid_setpoints);
	copy_arrays(state.wg_bias_cmd.setpoints.stub_setpoint.values, cmd.stub_setpoints);
	state.wg_bias_cmd.has_regulator_enable = true;
	state.wg_bias_cmd.regulator_enable = command_wgbias_reg_enable.read();
	state.wg_bias_cmd.has_do_readback = true;
	state.wg_bias_cmd.do_readback = command_wgbias_dac_read_update.read();
	state.wg_bias_st.device_present = status_wgbias_device_present.read();
	auto rb = status_wgbias_dac_values_readback.read();	//copy struct element by element
	copy_arrays(state.wg_bias_st.setpoints_readback.bulk_setpoint.values, rb.bulk_setpoints);
	copy_arrays(state.wg_bias_st.setpoints_readback.mid_setpoint.values, rb.mid_setpoints);
	copy_arrays(state.wg_bias_st.setpoints_readback.stub_setpoint.values, rb.stub_setpoints);
	state.wg_bias_st.device_error = status_wgbias_dac_error.read();
	status_wgbias_dac_error.acknowledge_reset(); //acknowledge communication errors

	//#### COMMS INTERFACE ####
	state.comms_st.comms_connected = status_comms_connected.read();
	state.comms_cmd.has_allow_connection = true;
	state.comms_cmd.allow_connection = command_comms_allow_connections.read();

	//=========================================================================

	//encode the message
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer.data(), encode_buffer.size());
	if(!pb_encode(&stream, App_Communication_fields, &message)) {
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
	App_Communication message = App_Communication_init_zero;

	//create the protobuf input stream type
	pb_istream_t stream = pb_istream_from_buffer(encoded_msg.data(), encoded_msg.size());

	//try to decode the message
	if(pb_decode(&stream, App_Communication_fields, &message)) {
		decode_err.write(true);
		decode_err_deserz++;
		return;
	}

	//check the message type
	if(message.which_payload != App_Communication_node_state_tag) {
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
	//#### MULTICARD STATUS ####
	if(new_state.multicard_cmd.has_sel_pd_input_aux_npic) {
		multicard_info_sel_input_aux_npic_command.publish(new_state.multicard_cmd.sel_pd_input_aux_npic);
	}

	//#### ONBOARD POWER MONITOR ###
	if(new_state.pm_onboard_cmd.has_regulator_enable) {
		pm_onboard_regulator_enable_command.publish(new_state.pm_onboard_cmd.regulator_enable);
	}

	//#### MOTHERBOARD POWER MONITOR ####
	if(new_state.pm_motherboard_cmd.has_regulator_enable) {
		pm_motherboard_regulator_enable_command.publish(new_state.pm_motherboard_cmd.regulator_enable);
	}

	//#### ADC OFFSET CONTROL ####
	if(new_state.offset_ctrl_cmd.has_offset_set) {
		write_sv_arrays(adc_offset_ctrl_dac_values_command, new_state.offset_ctrl_cmd.offset_set.values);
	}
	if(new_state.offset_ctrl_cmd.has_do_readback) {
		adc_offset_ctrl_perform_device_read_command.publish(new_state.offset_ctrl_cmd.do_readback);
	}

	//#### HISPEED SUBSYSTEM ####
	if(new_state.hispeed_cmd.has_SOA_DAC_drive) {
		write_sv_arrays(command_hispeed_SOA_DAC_drive, new_state.hispeed_cmd.SOA_DAC_drive.values);
	}
	if(new_state.hispeed_cmd.has_SOA_enable) {
		write_sv_arrays(command_hispeed_SOA_enable, new_state.hispeed_cmd.SOA_enable.values);
	}
	if(new_state.hispeed_cmd.has_TIA_enable) {
		write_sv_arrays(command_hispeed_TIA_enable, new_state.hispeed_cmd.TIA_enable.values);
	}
	if(new_state.hispeed_cmd.has_arm_request) {
		command_hispeed_arm_fire_request.publish(new_state.hispeed_cmd.arm_request);
	}
	if(new_state.hispeed_cmd.has_load_test_sequence) {
		command_hispeed_sdram_load_test_sequence.publish(new_state.hispeed_cmd.load_test_sequence);
	}

	//#### CoB TEMPERATURE MONITOR ####
	//no commands here

	//#### CoB EEPROM ####
	if(new_state.cob_eeprom_cmd.has_cob_desc_set) {
		//copy array into temporary app string, then assign
		App_String<sizeof(new_state.cob_eeprom_cmd.cob_desc_set)> temp_desc(new_state.cob_eeprom_cmd.cob_desc_set);
		command_cob_eeprom_write_contents.publish(temp_desc);
	}
	if(new_state.cob_eeprom_cmd.has_cob_write_key) {
		command_cob_eeprom_write_key.publish(new_state.cob_eeprom_cmd.cob_write_key);
	}
	if(new_state.cob_eeprom_cmd.has_do_cob_write_desc) {
		command_cob_eeprom_write.publish(new_state.cob_eeprom_cmd.do_cob_write_desc);
	}

	//#### WAVEGUIDE BIAS DRIVES ####
	if(new_state.wg_bias_cmd.has_do_readback) {
		command_wgbias_dac_read_update.publish(new_state.wg_bias_cmd.do_readback);
	}
	if(new_state.wg_bias_cmd.has_regulator_enable) {
		command_wgbias_reg_enable.publish(new_state.wg_bias_cmd.regulator_enable);
	}
	if(new_state.wg_bias_cmd.has_setpoints) {
		Waveguide_Bias_Drive::Waveguide_Bias_Setpoints_t sp = {0};								//temporary to copy setpoints into
		write_arrays(sp.stub_setpoints, new_state.wg_bias_cmd.setpoints.stub_setpoint.values);	//copy stubs into temp
		write_arrays(sp.mid_setpoints, new_state.wg_bias_cmd.setpoints.mid_setpoint.values);	//copy mids into temp
		write_arrays(sp.bulk_setpoints, new_state.wg_bias_cmd.setpoints.bulk_setpoint.values);	//copy bulks into temp
		command_wgbias_dac_values.publish(sp);	//copy the temp into the state variable
	}

	//#### COMMS INTERFACE ####
	if(new_state.comms_cmd.has_allow_connection) {
		command_comms_allow_connections.publish(new_state.comms_cmd.allow_connection);
	}
}






