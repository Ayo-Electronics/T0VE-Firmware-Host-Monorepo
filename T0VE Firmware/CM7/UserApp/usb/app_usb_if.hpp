/*
 * app_usb_if.hpp
 *
 *  Created on: Sep 18, 2025
 *      Author: govis
 *
 *  wrapper for TinyUSB functions basically
 */

#pragma once


#include "tusb.h" //tinyusb
#include "tinyusb/bsp/board_api.h" //tinyusb HAL

#include "app_scheduler.hpp"


class USB_Interface {
public:

	//===================== CONSTRUCTORS  ====================
	USB_Interface();

	//deleting copy constructor and assignment operator
	USB_Interface(const USB_Interface& other) = delete;
	void operator=(const USB_Interface& other) = delete;

	//=============== PUBLIC FUNCTIONS ===============
	//for now, just init
	void init();

	//lets downstream interfaces request a soft-disconnect of the USB device
	//useful (in our case) to disconnect USB when we're running the high-speed computation
	//and reconnecting the interface afterward
	void connect_request();
	void disconnect_request();

private:
	Scheduler tud_task_scheduler;
};


