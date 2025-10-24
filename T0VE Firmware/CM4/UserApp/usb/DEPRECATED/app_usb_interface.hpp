/*
 * app_usb_interface.hpp
 *
 *  Created on: Sep 17, 2025
 *      Author: govis
 */

#pragma once

#include "app_utils.hpp"
#include "app_types.hpp"
#include "app_usb_endpoint.hpp"

#include "usb_otg.h" //STM32 init code

class USB_Interface {
public:
	//========================== PUBLIC CONSTANTS ==========================

	static const size_t NUM_ENDPOINTS = 9;

	//============================= TYPEDEFS =============================
	struct USB_Hardware_Channel {
		PCD_HandleTypeDef* usb_handle;		//handle to pass to HAL functions
		USB_OTG_GlobalTypeDef* usb_global;	//handle to twiddle the GLOBAL usb registers
		USB_OTG_DeviceTypeDef* usb_device;	//handle to twiddle device-related USB registers
		//these are the functions that will be called to initialize SPI peripherals
		const Callback_Function<> usb_init_function;
		const Callback_Function<> usb_deinit_function;
	};

	static USB_Hardware_Channel USB_FS;


	//========================= CONSTRUCTORS, DESTRUCTORS =========================
	//normal constructor takes a reference to the USB hardware
	USB_Interface(USB_Hardware_Channel& _usb_hw);

	//delete copy constructor and assignment operator
	USB_Interface(const USB_Interface& other) = delete;
	void operator=(const USB_Interface& other) = delete;

	//========================= PUBLIC FUNCTIONS ========================
	//set the entire USB peripheral up
	void init();

private:
	//=============== PRIVATE FUNCTIONS ===============
	//###### INITIALIZATION HELPERS
	void core_init(); 				//RM0399 sec. 60.15.1
	void device_init(); 			//RM0399 sec. 60.15.3
	void ep_init_usb_reset(); 		//RM0399 sec. 60.15.6
	void ep_init_enum_cplt(); 		//RM0399 sec. 60.15.6
	void ep_init_set_address(uint8_t address);	//RM0399 sec. 60.15.6
	void ep_init_set_config_if(uint8_t cfg);	//RM0399 sec. 60.15.6
	void ep_activate();				//RM0399 sec. 60.15.6
	void ep_deactivate();			//RM0399 sec. 60.15.6

	//#### SEND STATUS IN
	//function to send status_in packet on EP0
	void status_in();


	//own a reference to the hardware
	USB_Hardware_Channel& usb_hw;

	//some FIFO sizing constants, units of words (4-bytes)
	static constexpr size_t RX_FIFO_SIZE = 128;
	static constexpr std::array<size_t, NUM_ENDPOINTS> TX_FIFO_SIZE = {{512, 32, 32, 32, 32, 32, 32, 32, 32}};
};
