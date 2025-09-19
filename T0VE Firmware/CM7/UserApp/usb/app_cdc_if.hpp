/*
 * app_cdc_if.hpp
 *
 *  Created on: Sep 19, 2025
 *      Author: govis
 */

#pragma once

class CDC_Interface {
public:

//########## CDC INTERFACE #########
//CDC_Interface(CDC_Interface_HW_t& cdc)		//save the CDC Interface hardware (provides a good compromise between abstraction from tinyUSB
												//								   but also ease of coding)

//void cdc_set_string_descriptor(Descriptor_t descriptor)	//set the string descriptor to send upon request from host

//bool cdc_connected()							//returns true if a terminal is connected to the CDC port
//void cdc_attach_rx_cb(Callback_Function<> cb)	//called when bytes available over CDC
//size_t cdc_rx_bytes_available()				//returns how many bytes available
//size_t cdc_rx_bytes_read(std::span)			//reads the bytes into the `span`, returns how many bytes read actually

//size_t cdc_tx_bytes_write(std::span)			//writes bytes into the CDC tx buffer
//size_t cdc_tx_bytes_avaialble()				//returns how many bytes available in the TX buffer
//bool cdc_tx_fifo_empty()						//returns true if the TX fifo is empty (i.e. not transmitting something)

//bool cdc_connect_request();					//ask if we can disconnect the USB port
//bool cdc_disconnect_request();

private:
};


