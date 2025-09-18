/*
 * app_debug_vcp.hpp
 *
 *  Created on: Jun 10, 2025
 *      Author: govis
 */

#pragma once

#include <string> //use C++ strings to print
#include <array> //for transmit buffer

//#include "usb_device.h" //to initialize peripheral
//#include "usbd_cdc_if.h" //for HAL CDC VCP device
#include "app_types.hpp"

static constexpr size_t APP_TX_DATA_SIZE = 512;

class VCP_Debug {
public:
	//just have these two methods for our limited use cases
	static void init(); //calls the CDC init function
	static void print(std::string text); //NOTE: going to write this as blocking!

private:
	VCP_Debug(); //don't allow instantiation of this class

	//and have a place to dump text data that we want to print
	//matches the size of the USB transmit buffer
	static std::array<uint8_t, APP_TX_DATA_SIZE> txbuf; //place for UART to put outgoing serial data
};
