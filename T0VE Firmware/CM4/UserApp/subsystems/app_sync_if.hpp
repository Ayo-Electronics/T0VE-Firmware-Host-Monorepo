/*
 * app_sync_node_id.hpp
 *
 *  Created on: Aug 15, 2025
 *      Author: govis
 *
 *  Simple class that aggregates some information about the multi-card system, so far:
 *  	- Node ID (just read on call to initialization)
 */

#pragma once

#include "app_proctypes.hpp"
#include "app_hal_gpio.hpp"

#include "app_scheduler.hpp" //for a bit of break-before-make for photodiode changeover
#include "app_threading.hpp"
#include "app_hal_pwm.hpp"


class Multicard_Info {
public:

	//constructor -- pass in pin map information for the node ID pins
	Multicard_Info(	GPIO::GPIO_Hardware_Pin _nid_0,
					GPIO::GPIO_Hardware_Pin _nid_1,
					GPIO::GPIO_Hardware_Pin _nid_2,
					GPIO::GPIO_Hardware_Pin _nid_3,
					GPIO::GPIO_Hardware_Pin _pres_intlk,
					GPIO::GPIO_Hardware_Pin _pic_pd_sel,
					GPIO::GPIO_Hardware_Pin _aux_pd_sel);

	//initialization function -- init the GPIOs and read the pin-strapped Node ID
	void init();

	//and functions to publish/subscribe to state variables across the system
	//returning/accepting references to avoid copying/assigning subscription channels
	SUBSCRIBE_FUNC(status_node_id);
	SUBSCRIBE_FUNC(status_all_cards_present);
	LINK_FUNC(command_sel_input_aux_npic);

private:
	//task to manage photodiode changeovers
	static const uint32_t PD_CHANGEOVER_PERIOD_MS = 500; //how much time we should allow for a source changeover
	void enable_pic_pd_sel();
	void enable_aux_pd_sel();
	void do_sel_pic_aux_pd(); //performing the AUX/PIC PD selection command update

	//a task to poll whether all cards are present
	static const uint32_t CARDS_PRESENT_CHECK_PERIOD_MS = 1000;
	void check_cards_present();

	//and a task to manage polling for state changes
	void check_state_update();

	//own some GPIO pins that read the state of the node ID pins, present interlock reading, and input source selection pins
	GPIO nid_0;
	GPIO nid_1;
	GPIO nid_2;
	GPIO nid_3;
	GPIO pres_intlk;
	GPIO pic_pd_sel;
	GPIO aux_pd_sel;

	//manage system state regarding node id and whether all cards are present
	PERSISTENT((Pub_Var<uint8_t>), status_node_id);
	PERSISTENT((Pub_Var<bool>), status_all_cards_present);

	//and subscribe to a state notification about whether we want to use the PIC photodiodes or the aux photodiodes as inputs
	Sub_Var<bool> command_sel_input_aux_npic; //FALSE if we're pulling from the PIC, TRUE if from aux

	//a little scheduler for checking for state updates and break-before-make input source changeover
	Scheduler check_state_update_task;
	Scheduler check_cards_present_task;
	Scheduler connect_pd_source;
};
