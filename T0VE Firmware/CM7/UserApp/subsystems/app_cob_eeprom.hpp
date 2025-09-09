/*
 * app_cop_eeprom.hpp
 *
 *  Created on: Aug 15, 2025
 *      Author: govis
 */

#pragma once

#include "app_hal_i2c.hpp" //for the I2C bus to pass to the TMP117 class
#include "app_scheduler.hpp" //for the scheduler task
#include "app_state_variable.hpp" //for shared states
#include "app_state_machine_library.hpp" //for enable/disabled state machine

#include "app_24AA02UID.hpp"

class CoB_EEPROM {
public:
    //constructor--take an I2C bus we'll pass to the EEPROM class
	CoB_EEPROM(Aux_I2C& _bus);

    //initialization routine
    void init();

    //and basically the only other thing we need for this subsystem is getting the state control handles
    //use the macros in the state variable header to generate these easily
    SUBSCRIBE_FUNC(status_device_present);
    SUBSCRIBE_FUNC(status_cob_eeprom_UID);
    SUBSCRIBE_FUNC(status_cob_eeprom_contents)
    SUBSCRIBE_FUNC_RC(status_cob_eeprom_write_error);
    LINK_FUNC_RC(command_cob_eeprom_write_contents);
    LINK_FUNC_RC(command_cob_eeprom_write);
    LINK_FUNC_RC(command_cob_eeprom_write_key);
    LINK_FUNC(status_onboard_pgood);

    //delete the copy constructor and assignment operator
    CoB_EEPROM(const CoB_EEPROM& other) = delete;
    void operator=(const CoB_EEPROM& other) = delete;

private:
    //enable/disable function that sets everything up if power is good
    void disable();	//disabled state entry function
    void enable();	//disabled state exit function

    //main thread function, just runs the state machine basically
    Scheduler check_state_update_task;
    void check_state_update();

    //functions and variables related to EEPROM writing
    //own a temporary array where we can dump write contents, so we don't have a huge stack allocation
    //also own a bool flag saying that we're writing
    std::array<uint8_t, EEPROM_24AA02UID::MEMORY_SIZE_BYTES> write_contents_temp;
    bool eeprom_writing;		//have a flag variable saying that we're actively writing
    size_t write_index;			//have an index that we're writing
    static constexpr uint32_t WRITE_ACCESS_KEY = 0xA110CA7E; //a cute little key to allow for EEPROM writing
    void eeprom_write_start();	//this is the write state entry function
    void eeprom_write_finish();	//this is the write state exit function
    void eeprom_write_do(); 		//this function gets called repeatedly to write the EEPROM page-by-page
    void eeprom_write_error();		//called if there is any I2C bus write error
    Scheduler eeprom_write_task; 	//and this is the scheduling function that calls the write-page task

    //own an EEPROM
    EEPROM_24AA02UID eeprom;

    //shared state variables
    State_Variable<bool> status_device_present; 		//report whether we detected the device during initialization
    State_Variable<uint32_t> status_cob_eeprom_UID;		//report the UID reported by the EEPROM connected to the CoB
    State_Variable<std::array<uint8_t, EEPROM_24AA02UID::MEMORY_SIZE_BYTES>> status_cob_eeprom_contents; //user RW section of the eeprom
    State_Variable<bool> status_cob_eeprom_write_error;	//report any issues that happened during eeprom write
    SV_Subscription_RC<std::array<uint8_t, EEPROM_24AA02UID::MEMORY_SIZE_BYTES>> command_cob_eeprom_write_contents; //what to write to the eeprom
    SV_Subscription_RC<bool> command_cob_eeprom_write;			//command to write to the CoB EEPROM
    SV_Subscription_RC<uint32_t> command_cob_eeprom_write_key; 	//only write to the EEPROM if the keys match
    SV_Subscription<bool> status_onboard_pgood; //whether motherboard CoB supplies are up--using onboard supplies as proxy
    //This can technically result in a failure mode where the 3.3V rail fails, but power is still reported as good
    //locks up the I2C bus and potentially back-powers devices (Not really worrying about this failure mode since non-catastrophic and not common)
    //However if we really cared TODO: Fix hardware such that either
    //	a) [MOTHERBOARD_FIX] CoB and DACs are driven by the same supply rail as the arduinos
    //	b) [MOTHERBOARD_FIX] motherboard PGOOD signals are permanently tied to GND for non-aux cards
    //	c) [PROCESSOR_CARD_FIX] processor card DAC runs on different I2C bus as motherboard I2C lines

    //and a basic state machine to cycle between enabled/disabled/writing depending on PGOOD + command status
	ESM_State eeprom_state_ENABLED;
	ESM_State eeprom_state_DISABLED;
	ESM_State eeprom_state_WRITING;
	bool trans_ENABLE_to_DISABLE() 	{ return !status_onboard_pgood; }	//check our subscription variable to see if power is bad
	bool trans_DISABLE_to_ENABLE() 	{ return status_onboard_pgood; 	}	//check our subscription variable to see if power is good
	bool trans_WRITING_to_ENABLE() 	{ return !eeprom_writing; 	}		//return to the idle enable state after we finished writing to the EEPROM
	bool trans_WRITING_to_DISABLE()	{ return !status_onboard_pgood;	}	//return to the idle enable state if power fails during write
	bool trans_ENABLE_to_WRITING()	{
		//check if we received a write command and the write key is correct
		//if so, don't acknowledge the command yet to signify that we're writing
		if(command_cob_eeprom_write && (command_cob_eeprom_write_key == WRITE_ACCESS_KEY))	return true;

		//otherwise, always acknowledge/clear the commands to reset them
		command_cob_eeprom_write_contents.acknowledge_reset();
		command_cob_eeprom_write_key.acknowledge_reset();
		command_cob_eeprom_write.acknowledge_reset();
		return false;
	}
	ESM_Transition trans_from_ENABLED[2] = {	{&eeprom_state_DISABLED, {BIND_CALLBACK(this, trans_ENABLE_to_DISABLE)}		},
												{&eeprom_state_WRITING,	 {BIND_CALLBACK(this, trans_ENABLE_to_WRITING)}		}	};
	ESM_Transition trans_from_DISABLED[1] = {	{&eeprom_state_ENABLED, {BIND_CALLBACK(this, trans_DISABLE_to_ENABLE)}		}	};
	ESM_Transition trans_from_WRITING[2] = {	{&eeprom_state_ENABLED, {BIND_CALLBACK(this, trans_WRITING_to_ENABLE)}		},
												{&eeprom_state_DISABLED, {BIND_CALLBACK(this, trans_WRITING_to_DISABLE)}	}	};
	Extended_State_Machine esm;
	Scheduler esm_exec_task; //and a scheduler to call the `run_esm` function
};

