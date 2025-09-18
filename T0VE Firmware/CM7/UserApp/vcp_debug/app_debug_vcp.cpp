/*
 * app_debug_vcp.cpp
 *
 *  Created on: Jun 10, 2025
 *      Author: govis
 */


#include "app_debug_vcp.hpp"
#include "app_hal_tick.hpp" //for delay function

//=============================== STATIC MEMBER INITIALIZATION ================================

std::array<uint8_t, APP_TX_DATA_SIZE> VCP_Debug::txbuf; //place for UART to put outgoing serial data

//=============================== CLASS FUNCTIONS ================================

//just call the HAL initialization function here
void VCP_Debug::init() {
	//MX_USB_DEVICE_Init();
}

//USB printing function
void VCP_Debug::print(std::string text) {
	//just return if we can't fit our text into our transmit buffer
	if(text.size() > txbuf.size()) return;

	//copy the string into our intermediate buffer
	std::copy(text.begin(), text.end(), txbuf.begin());

	//keep trying to transmit the result
//	uint8_t result;
//	do {
//		result = CDC_Transmit_FS(txbuf.begin(), text.size());
//	} while(result == USBD_BUSY);
}
