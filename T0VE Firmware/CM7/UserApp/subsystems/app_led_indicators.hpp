/*
 * app_led_indicators.hpp
 *
 *  Created on: Aug 21, 2025
 *      Author: govis
 *
 *  Intent behind this class is to flash the onboard LEDs to convey some meaningful message of system status
 *  Right now I'm thinking (in order of priority)
 *   - OFF --> RESET
 *   - BLUE --> not PGOOD
 *   - GREEN --> power good, analog on
 *   - MAGENTA --> comms activity
 *   - RED --> ARMed
 *
 *  And mirror the following functionality to the activity LEDs
 *   - ACT 1 --> PGOOD
 *   - ACT 2 --> comms activity
 *
 */

#pragma once

#include "app_hal_gpio.hpp"
#include "app_state_variable.hpp"
#include "app_scheduler.hpp"

class LED_Indicators
{
public:

	LED_Indicators(	GPIO::GPIO_Hardware_Pin red_led_pin,
					GPIO::GPIO_Hardware_Pin green_led_pin,
					GPIO::GPIO_Hardware_Pin blue_led_pin,
					GPIO::GPIO_Hardware_Pin act_1_pin,
					GPIO::GPIO_Hardware_Pin act_2_pin,
					bool _RGB_inverted = true,
					bool _activity_inverted = false);

	//initialize the GPIO hardware, start scheduler/monitor threads
	void init();

	//and finally some functions that link state variables
	//need to know the following:
	// --> armed status
	// --> comms activity
	// --> pgood status
	LINK_FUNC(status_onboard_pgood);
	LINK_FUNC(status_hispeed_armed);
	LINK_FUNC_RC(status_comms_activity);

private:
	//own some GPIO pins for LED control
	GPIO red_led;
	GPIO green_led;
	GPIO blue_led;
	bool RGB_inverted;

	GPIO activity_1;
	GPIO activity_2;
	bool activity_inverted;

	//and some quick inline functions to set/clear LEDs
	__attribute__((always_inline)) void RED_ON()	{ if(RGB_inverted) red_led.clear(); else red_led.set(); }
	__attribute__((always_inline)) void RED_OFF()	{ if(RGB_inverted) red_led.set(); else red_led.clear(); }
	__attribute__((always_inline)) void GREEN_ON()	{ if(RGB_inverted) green_led.clear(); else green_led.set(); }
	__attribute__((always_inline)) void GREEN_OFF()	{ if(RGB_inverted) green_led.set(); else green_led.clear(); }
	__attribute__((always_inline)) void BLUE_ON()	{ if(RGB_inverted) blue_led.clear(); else blue_led.set(); }
	__attribute__((always_inline)) void BLUE_OFF()	{ if(RGB_inverted) blue_led.set(); else blue_led.clear(); }

	//some RGB mixing functions
	__attribute__((always_inline)) void ONBOARD_LED_RED() 		{ RED_ON(); GREEN_OFF(); BLUE_OFF(); }
	__attribute__((always_inline)) void ONBOARD_LED_GREEN() 	{ RED_OFF(); GREEN_ON(); BLUE_OFF(); }
	__attribute__((always_inline)) void ONBOARD_LED_BLUE() 		{ RED_OFF(); GREEN_OFF(); BLUE_ON(); }
	__attribute__((always_inline)) void ONBOARD_LED_YELLOW() 	{ RED_ON(); GREEN_ON(); BLUE_OFF(); }
	__attribute__((always_inline)) void ONBOARD_LED_MAGENTA() 	{ RED_ON(); GREEN_OFF(); BLUE_ON(); }
	__attribute__((always_inline)) void ONBOARD_LED_CYAN() 		{ RED_OFF(); GREEN_ON(); BLUE_ON(); }
	__attribute__((always_inline)) void ONBOARD_LED_WHITE() 	{ RED_ON(); GREEN_ON(); BLUE_ON(); }
	__attribute__((always_inline)) void ONBOARD_LED_OFF() 		{ RED_OFF(); GREEN_OFF(); BLUE_OFF(); }
	

	__attribute__((always_inline)) void ACT1_ON()	{ if(activity_inverted) activity_1.clear(); else activity_1.set(); }
	__attribute__((always_inline)) void ACT1_OFF()	{ if(activity_inverted) activity_1.set(); else activity_1.clear(); }
	__attribute__((always_inline)) void ACT2_ON()	{ if(activity_inverted) activity_2.clear(); else activity_2.set(); }
	__attribute__((always_inline)) void ACT2_OFF()	{ if(activity_inverted) activity_2.set(); else activity_2.clear(); }

	//and private functions that manage the update of onboard/offboard LEDs
	void update_onboard_LEDs();
	void update_offboard_LEDs();

	//and private functions that check/manage system state updates
	void check_state_update();
	void acknowledge_comms_activity(); //called by scheduler after comms activity

	//own some schedulers to flash/blink LEDs
	Scheduler check_state_update_task; //scheduler that checks for updates to state variables
	Scheduler finish_comms_flash_task; //scheduler that deasserts LEDs after a certain time period to signal comms activity
	static const uint32_t COMMS_ACTIVITY_FLASH_TIME_MS = 100;

	//and some status variables according to which we'll flash LEDs
	//pull these variables from other internal subsystems
	SV_Subscription<bool> status_onboard_pgood;
	SV_Subscription_RC<bool> status_comms_activity;
	SV_Subscription<bool> status_hispeed_armed;
};
