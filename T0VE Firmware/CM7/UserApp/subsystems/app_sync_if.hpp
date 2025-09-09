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

#include "app_hal_gpio.hpp"

#include "app_scheduler.hpp" //for a bit of break-before-make for photodiode changeover
#include "app_state_variable.hpp"
#include "app_types.hpp"
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
					GPIO::GPIO_Hardware_Pin _aux_pd_sel,
					GPIO::GPIO_Hardware_Pin _node_ready,
					GPIO::GPIO_Hardware_Pin _all_ready,
					PWM::PWM_Hardware_Channel& _syncout_timer,
					PWM::PWM_Hardware_Channel& _syncin_timer);

	//initialization function -- init the GPIOs and read the pin-strapped Node ID
	void init();

	//functions that the node can use, specifically for the high-speed section
	void configure_sync_timer(float frequency, float duty);
	void enable_sync_timer(); 	//enables syncin timer then syncout timer
	void disable_sync_timer();	//disables synout timer then syncin timer

	//optimize these functions
	//just simple I/O accesses that can be fast; performed in high-speed loop
	#pragma GCC push_options
	#pragma GCC optimize ("Ofast,unroll-loops,inline-functions")

	__attribute__((always_inline)) inline void node_is_ready() 	{ node_ready.clear(); } //active low
	__attribute__((always_inline)) inline void node_not_ready() { node_ready.set();	} 	//active low
	__attribute__((always_inline)) inline bool get_all_ready() 	{ return all_ready.read(); } //active high
	__attribute__((always_inline)) inline uint32_t get_sync_triggered() { return syncin_timer.get_triggered(); }
	__attribute__((always_inline)) inline void reset_sync_triggered()	{ syncin_timer.reset_triggered(); }

	#pragma GCC pop_options

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
	GPIO node_ready;
	GPIO all_ready;

	//and own timers for syncout and syncin triggering
	PWM syncout_timer;
	PWM syncin_timer;

	//manage system state regarding node id and whether all cards are present
	State_Variable<uint8_t> status_node_id;
	State_Variable<bool> status_all_cards_present;

	//and subscribe to a state notification about whether we want to use the PIC photodiodes or the aux photodiodes as inputs
	SV_Subscription<bool> command_sel_input_aux_npic; //FALSE if we're pulling from the PIC, TRUE if from aux

	//a little scheduler for checking for state updates and break-before-make input source changeover
	Scheduler check_state_update_task;
	Scheduler check_cards_present_task;
	Scheduler connect_pd_source;
};
