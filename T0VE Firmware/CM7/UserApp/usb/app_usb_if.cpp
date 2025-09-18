/*
 * app_usb_if.cpp
 *
 *  Created on: Sep 18, 2025
 *      Author: govis
 */

#include "app_usb_if.hpp"

USB_Interface::USB_Interface() {}


void USB_Interface::init() {
	//not strictly necessary to call the TinyUSB version of `board_init()`
	//this just calls the HAL usb init
	board_init();

	//initialize TinyUSB
	tusb_rhport_init_t dev_init = {
			.role = TUSB_ROLE_DEVICE,
			.speed = TUSB_SPEED_AUTO
	};
	tusb_init(BOARD_DEVICE_RHPORT_NUM, &dev_init);

	//check if TinyUSB wants to do anything after board init
	if (board_init_after_tusb) {
		board_init_after_tusb();
	}

	//schedule the TinyUSB device task to run every loop iteration
	//I think this is due to ISR servicing being deferred to main loop
	tud_task_scheduler.schedule_interval_ms(tud_task, Scheduler::INTERVAL_EVERY_ITERATION);
}
