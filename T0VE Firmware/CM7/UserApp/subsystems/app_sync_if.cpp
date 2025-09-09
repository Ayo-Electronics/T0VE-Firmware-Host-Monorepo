/*
 * app_sync_if.cpp
 *
 *  Created on: Aug 15, 2025
 *      Author: govis
 */


#include "app_sync_if.hpp"

//================================= PUBLIC FUNCTION DEFINITIONS ===============================
Multicard_Info::Multicard_Info(	GPIO::GPIO_Hardware_Pin _nid_0,
                                GPIO::GPIO_Hardware_Pin _nid_1,
                                GPIO::GPIO_Hardware_Pin _nid_2,
                                GPIO::GPIO_Hardware_Pin _nid_3,
								GPIO::GPIO_Hardware_Pin _pres_intlk,
								GPIO::GPIO_Hardware_Pin _pic_pd_sel,
								GPIO::GPIO_Hardware_Pin _aux_pd_sel,
								GPIO::GPIO_Hardware_Pin _node_ready,
								GPIO::GPIO_Hardware_Pin _all_ready,
								PWM::PWM_Hardware_Channel& _syncout_timer,
								PWM::PWM_Hardware_Channel& _syncin_timer) :
	nid_0(_nid_0),
    nid_1(_nid_1),
    nid_2(_nid_2),
    nid_3(_nid_3),
	pres_intlk(_pres_intlk),
	pic_pd_sel(_pic_pd_sel),
	aux_pd_sel(_aux_pd_sel),
	node_ready(_node_ready),
	all_ready(_all_ready),
	syncout_timer(_syncout_timer),
	syncin_timer(_syncin_timer)
{}


/*
 * `init()`
 * - initializes the GPIOs and reads the pin-strapped Node ID
 */
void Multicard_Info::init() {
	nid_0.init();
	nid_1.init();
	nid_2.init();
	nid_3.init();
	pres_intlk.init();
	pic_pd_sel.init();
	aux_pd_sel.init();
	node_ready.init();
	all_ready.init();

	//defer initialization of the timers into `configure_sync_timer` function
	//	syncout_timer.init();
	//	syncin_timer.init();

	//node is not ready
	node_not_ready();

    //read the GPIO pins to determine the node ID
    status_node_id = (nid_0.read() << 0) | (nid_1.read() << 1) | (nid_2.read() << 2) | (nid_3.read() << 3);

    //set up the pic/aux photodiode source selection to what we'd like by default
    do_sel_pic_aux_pd();

    //start the task that monitors whether all cards are present
    check_cards_present_task.schedule_interval_ms(BIND_CALLBACK(this, check_cards_present), CARDS_PRESENT_CHECK_PERIOD_MS);

    //and start the task that monitors state changes - runs every iteration
    check_state_update_task.schedule_interval_ms(BIND_CALLBACK(this, check_state_update), Scheduler::INTERVAL_EVERY_ITERATION);
}

void Multicard_Info::configure_sync_timer(float frequency, float duty) {
	syncout_timer.init();
	syncin_timer.init();
	syncout_timer.set_frequency(frequency);
	syncout_timer.set_duty(duty);
}

void Multicard_Info::enable_sync_timer() {
	//enable the syncin timer first
	//so we can immediately trigger downstream timers
	//if we're the one responsible for the sync signal
	//also make sure to reset the counter on the SYNCIN timer so we don't have any short pulses
	syncout_timer.reset_count(0xFFFF);
	syncin_timer.enable();
	syncout_timer.enable();
}

void Multicard_Info::disable_sync_timer() {
	//disable the syncout timer first
	//stops triggering the syncin sequence immediately
	syncout_timer.disable();
	syncin_timer.disable();
}

//================================= PRIVATE FUNCTION DEFS ================================

void Multicard_Info::check_cards_present() {
    status_all_cards_present = !pres_intlk.read(); //inverted polarity; hard-coding for now
}

void Multicard_Info::enable_pic_pd_sel() {
	pic_pd_sel.set(); //positive polarity, hard-coding for now
}

void Multicard_Info::enable_aux_pd_sel() {
	aux_pd_sel.set(); //positive polarity, hard-coding for now
}

void Multicard_Info::do_sel_pic_aux_pd() {
	//IF we want to enable our input source to be from the auxiliary photodiodes
	if(command_sel_input_aux_npic) {
		pic_pd_sel.clear(); //immediately disable the PIC photodiode source
		//and changeover to the AUX photodiode source after the changeover period
		connect_pd_source.schedule_oneshot_ms(BIND_CALLBACK(this, enable_aux_pd_sel), PD_CHANGEOVER_PERIOD_MS);
	}

	//OTHERWISE we want to enable our input source to be from the pic photodiodes
	else {
		aux_pd_sel.clear(); //immediately disable the auxiliary photodiode source
		//and changeover to the PIC photodiode source after the changeover period
		connect_pd_source.schedule_oneshot_ms(BIND_CALLBACK(this, enable_pic_pd_sel), PD_CHANGEOVER_PERIOD_MS);
	}
}

void Multicard_Info::check_state_update() {
	//check if we've received a new command for the input port selection
	if(command_sel_input_aux_npic.available())
		//defer command actualization to another function
		do_sel_pic_aux_pd();

}
