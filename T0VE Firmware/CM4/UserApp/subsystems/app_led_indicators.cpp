/*
 * app_led_indicators.cpp
 *
 *  Created on: Aug 21, 2025
 *      Author: govis
 */

#include "app_led_indicators.hpp"

//========================================== PUBLIC FUNCTIONS ==========================================

LED_Indicators::LED_Indicators(	GPIO::GPIO_Hardware_Pin red_led_pin,
									GPIO::GPIO_Hardware_Pin green_led_pin,
									GPIO::GPIO_Hardware_Pin blue_led_pin,
									GPIO::GPIO_Hardware_Pin act_1_pin,
									GPIO::GPIO_Hardware_Pin act_2_pin,
									bool _RGB_inverted,
									bool _activity_inverted):
	//save the arduino onboard RGB LEDs
	red_led(red_led_pin),
	green_led(green_led_pin),
	blue_led(blue_led_pin),
	RGB_inverted(_RGB_inverted),

	//and save the external activity LEDs
	activity_1(act_1_pin),
	activity_2(act_2_pin),
	activity_inverted(_activity_inverted)
{}

void LED_Indicators::init() {
	//initialize our hardware
    red_led.init();
    green_led.init();
    blue_led.init();
    activity_1.init();
    activity_2.init();

    //put the LEDs into their default states
    update_offboard_LEDs();
    update_onboard_LEDs();

    //and schedule our state update task
    check_state_update_task.schedule_interval_ms(BIND_CALLBACK(this, check_state_update), Scheduler::INTERVAL_EVERY_ITERATION);
}

//========================================== THREAD FUNCTIONS ==========================================
void LED_Indicators::check_state_update() {
    //flag variable to check if we need to update the status of the LEDs
    bool do_update_leds = false;

    //check for any state updates using `available()`
    if(status_hispeed_armed.check()) 					do_update_leds = true;
    if(status_hispeed_arm_flag_err_ready.check()) 		do_update_leds = true;
    if(status_hispeed_arm_flag_err_timeout.check()) 	do_update_leds = true;
    if(status_hispeed_arm_flag_err_cancelled.check()) 	do_update_leds = true;
    if(status_hispeed_arm_flag_err_pwr.check()) 		do_update_leds = true;
    if(status_onboard_pgood.check()) 					do_update_leds = true;
    if(status_motherboard_pgood.check()) 				do_update_leds = true;
    if(status_comms_connected.check()) 					do_update_leds = true;
    if(status_comms_activity.check()) {
        //update the LEDs now
        do_update_leds = true;
        //and schedule an acknowledgement of the comms activity later
        //makes sure we see a perceivable flash
        finish_comms_flash_task.schedule_oneshot_ms(BIND_CALLBACK(this, acknowledge_comms_activity), COMMS_ACTIVITY_FLASH_TIME_MS);
    }

    //if we need to update the LEDs, do so
    if(do_update_leds) {
        update_onboard_LEDs();
        update_offboard_LEDs();
    } 
}

void LED_Indicators::acknowledge_comms_activity() {
    //acknowledge the comms activity flag--> resets its state
    //and update onboard + offboard LEDs
    status_comms_activity.acknowledge_reset();
    update_onboard_LEDs();
    update_offboard_LEDs();
}

//========================================== LED UPDATE FUNCTIONS ==========================================

void LED_Indicators::update_onboard_LEDs() {
    //update onboard RGB LEDs depending on system state
    //implicit prioritization given the ordering of these conditionals
    if(status_hispeed_armed.read())                            	ONBOARD_LED_MAGENTA();
    else if(status_comms_activity.read())                      	ONBOARD_LED_WHITE();
    else if(status_hispeed_arm_flag_err_ready.read())          	ONBOARD_LED_RED();
    else if(status_hispeed_arm_flag_err_timeout.read())   		ONBOARD_LED_RED();
    else if(status_hispeed_arm_flag_err_pwr.read())            	ONBOARD_LED_RED();
    else if(status_hispeed_arm_flag_err_cancelled.read())		ONBOARD_LED_RED();
    else if(status_onboard_pgood.read())                       	ONBOARD_LED_GREEN();
    else if(status_comms_connected.read())                     	ONBOARD_LED_YELLOW();
    else                                                		ONBOARD_LED_BLUE();
}

void LED_Indicators::update_offboard_LEDs() {
    //update activity 1 LED depending on the power good status
    if(status_motherboard_pgood.read()) ACT1_ON();
    else ACT1_OFF();

    //update activity 2 LED depending on the comms activity status
    if(status_comms_activity.read()) ACT2_ON();
    else ACT2_OFF();
}
