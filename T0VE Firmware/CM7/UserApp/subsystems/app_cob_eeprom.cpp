/*
 * app_cob_eeprom.cpp
 *
 *  Created on: Aug 15, 2025
 *      Author: govis
 */

#include "app_cob_eeprom.hpp"

//start with the constructor
CoB_EEPROM::CoB_EEPROM(Aux_I2C& _bus):
	//construct our EEPROM with the I2C bus
	eeprom(_bus),

	//construct our states and state machine
	eeprom_state_ENABLED({}, {}, {}),
	eeprom_state_DISABLED(BIND_CALLBACK(this, disable), {}, BIND_CALLBACK(this, enable)),
	eeprom_state_WRITING(BIND_CALLBACK(this, eeprom_write_start),
						 {}, //schedule a task in the `eeprom_write_start` that actually does the writing
						 BIND_CALLBACK(this, eeprom_write_finish)),
	esm(&eeprom_state_DISABLED)
{
	//link the transitions from each state
	eeprom_state_DISABLED.attach_state_transitions(trans_from_DISABLED);
	eeprom_state_ENABLED.attach_state_transitions(trans_from_ENABLED);
	eeprom_state_WRITING.attach_state_transitions(trans_from_WRITING);
}


void CoB_EEPROM::init() {
	//init just starts the esm in its thread function
	//binding the `RUN_ESM` function directly for a little less overhead
	check_state_update_task.schedule_interval_ms(	BIND_CALLBACK(&esm, Extended_State_Machine::RUN_ESM),
													Scheduler::INTERVAL_EVERY_ITERATION);
}

//========================== PRIVATE FUNCTIONS ==========================
void CoB_EEPROM::enable() {
	//initialize the eeprom
	eeprom.init();

	//and save information related to the EEPROM in state variables
	status_device_present = eeprom.check_presence();
	status_cob_eeprom_UID = eeprom.get_UID();
	status_cob_eeprom_contents = eeprom.get_contents();

	//and clear any stale desires to write to the EEPROM
	command_cob_eeprom_write.acknowledge_reset();
	command_cob_eeprom_write_key.acknowledge_reset();
	command_cob_eeprom_write_contents.acknowledge_reset();
}

void CoB_EEPROM::disable() {
	//deinit the device
	eeprom.deinit();

	//and clear any unserviced desires to write to the EEPROM
	command_cob_eeprom_write.acknowledge_reset();
	command_cob_eeprom_write_key.acknowledge_reset();
	command_cob_eeprom_write_contents.acknowledge_reset();

	//and clear the status variables to default values
	status_cob_eeprom_UID = 0;
	status_cob_eeprom_contents = {0};
	status_cob_eeprom_write_error = 0;
	status_device_present = 0;
}

//##### EEPROM WRITING FUNCTIONS ######
void CoB_EEPROM::eeprom_write_error() {
	//signal that we had a write error using the flag
	status_cob_eeprom_write_error = true;

	//and exit the writing state by clearing our writing flag
	eeprom_writing = false;
}

void CoB_EEPROM::eeprom_write_start() {
	//copy the state variable into the temporary and clear them
	write_contents_temp = command_cob_eeprom_write_contents;

	//and reset our array index to the start of the EEPROM
	//and set the flag variable to indicate that we're writing
	write_index = 0;
	eeprom_writing = true;

	//and now we send the first page's worth of data
	//the `do_write` function will reschedule itself appropriately
	eeprom_write_do();
}

void CoB_EEPROM::eeprom_write_finish() {
	//acknowledge that we finished writing to the EEPROM by clearing the command flags
	command_cob_eeprom_write.acknowledge_reset();
	command_cob_eeprom_write_key.acknowledge_reset();
	command_cob_eeprom_write_contents.acknowledge_reset();
}

void CoB_EEPROM::eeprom_write_do() {
	//this function is called repeatedly to write the contents of the EEPROM page-by-page
	//	\--> increments the page address automatically (check first thing though so we don't go outta bounds)
	//	\--> section the write contents depending on the page
	//	\--> write the contents to the EEPROM
	//	\--> reschedule task
	//--> errors out on write/ACK fail, automatically terminates write
	static const size_t PG_SIZE = EEPROM_24AA02UID::PAGE_SIZE_BYTES;

	//start by checking if we're outta bounds
	//return if we're outta bounds (or if writing stopped due to recent error)
	if((write_index + PG_SIZE) > EEPROM_24AA02UID::MEMORY_SIZE_BYTES) eeprom_writing = false;
	if(!eeprom_writing) return;

	//all good, section the write contents appropriately
	//creating fixed sized span from c-array version of the data + necessary offsets
	//a little funky syntax, but no cleaner way really
	std::span<uint8_t, PG_SIZE> pg(write_contents_temp.data() + write_index, PG_SIZE);

	//perform the write
	auto scheduled = eeprom.write_page(write_index, pg, BIND_CALLBACK(this, eeprom_write_error));

	//if the write was successfully staged, we can write the following page in a little bit
	if(scheduled) {
		//increment the array address for the next iteration
		//and reschedule after a reasonable amount of time to let the write complete
		write_index += PG_SIZE;
		eeprom_write_task.schedule_oneshot_ms(BIND_CALLBACK(this, eeprom_write_do), EEPROM_24AA02UID::WRITE_CYCLE_TIME_MS);
	}

	//but if the transmission wasn't, try writing again next iteration
	else
		eeprom_write_task.schedule_oneshot_ms(BIND_CALLBACK(this, eeprom_write_do), Scheduler::ONESHOT_NEXT_ITERATION);
}
