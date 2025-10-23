/*
 * app_cdc_if.hpp
 *
 *  Created on: Sep 19, 2025
 *      Author: govis
 *
 *  NOTE: current implementation is not thread safe!
 *  Adding mutex guards and thread safety significantly turns up bloat for stuff that will largely be configured once.
 *  Task for future me is to modify this for thread safety as necessary
 */

#pragma once

#include "app_proctypes.hpp"
#include "app_utils.hpp"
#include "app_usb_if.hpp"
#include "app_threading.hpp"

#include "tusb.h"
#include "tusb_config.h"

class CDC_Interface {
public:
	//================== TYPEDEFS ==================

	//for the line coding struct
	enum Stop_Bits : uint8_t { SB_1 = 0, SB_1p5 = 1, SB_2 = 2 };
	enum Parity : uint8_t { NONE = 0, ODD = 1, EVEN = 2, MARK = 3, SPACE = 4 };
	struct CDC_Line_Coding_t {
		uint32_t baud_rate;
		Stop_Bits stop_bits;
		Parity parity;
		uint8_t data_bits;

		//need comparison operator for Pub_Var
		bool operator==(const CDC_Line_Coding_t& other) {
			return 	this->baud_rate == other.baud_rate &&
					this->stop_bits == other.stop_bits &&
					this->parity == other.parity &&
					this->data_bits == other.data_bits;
		}
	};
	struct Flow_Control_t {
		Atomic_Var<bool> dtr_status;	//set if host present, use as "host connected" indicator (that's what TinyUSB does at least)
		Atomic_Var<bool> rts_status;	//not really useful, set if host wants to transmit data
		Atomic_Var<bool> dcd_status;	//device is physically present
		Atomic_Var<bool> dsr_status;	//device is ready to accept data
	};

	//way to describe a particular CDC interface
	//includes most of the state management inside here
	struct CDC_Interface_Channel_t {
		//if we have more than one CDC interface, this is how we identify a particular one
		//using "interface" in the colloquial sense, not the usb one
		const size_t cdc_itf_no;

		//flow control status and line coding
		//make sure these update atomically for thread safety
		Flow_Control_t flow_control;
		Pub_Var<CDC_Line_Coding_t>& line_coding;

		//and have some callback functions for events
		Callback_Function<> flow_control_change_cb;
		Callback_Function<> coding_change_cb;
		Callback_Function<> rx_available_cb;
	};

	//and for now, a single static channel
	static CDC_Interface_Channel_t CDC_CHANNEL;

	//================= PUBLIC FUNCTIONS ==================
	//takes care of general housekeeping at startup
	void init();

	//hooks for callback functions
	//make sure to initialize them before calling `connect_request()`!
	//could result in a nasty race condition where you seg fault due to poorly formed memory addresses
	void register_flow_control_change_cb(Callback_Function<> cb);
	void register_coding_change_cb(Callback_Function<> cb);
	void register_rx_available_cb(Callback_Function<> cb);

	//receive-side functions
	size_t rx_bytes_available();				//returns how many bytes available
	size_t rx_bytes_read(std::span<uint8_t, std::dynamic_extent> read_buffer);	//reads the bytes into the `span`, returns how many bytes read actually

	//transmit-side functions
	//writes bytes into the CDC tx buffer, `immediate` flag says if we should flush the TX FIFO immediately after transfer
	//or wait for the TUSB task to dispatch automatically
	//useful if we wanna send short packets or zero-length packets over USB
	size_t tx_bytes_write(std::span<uint8_t, std::dynamic_extent> transmit_buffer, bool immediate = false);
	size_t tx_bytes_available();	//returns how many bytes available in the TX buffer
	bool tx_fifo_empty();			//returns true if the TX fifo is empty (i.e. not transmitting something)

	//connection-related functions
	bool connected();						//returns true if a terminal is connected to the CDC port
	CDC_Line_Coding_t get_line_coding();	//return the line coding we're currently configured with
	void connect_request();			//ask if we can connect the serial interface
	void disconnect_request();		//ask if we can disconnect the serial interface
	void set_ready();				//flow control change--set the DSR bit
	void set_busy();				//flow control change--clear DSR bit


	//================ CONSTRUCTORS ===============
	//construct with an upstream USB interface, and CDC channel
	CDC_Interface(USB_Interface& _usb_if, CDC_Interface_Channel_t& _cdc_channel);

	//and delete copy constructor and assignment operator
	CDC_Interface(const CDC_Interface& other) = delete;
	void operator=(const CDC_Interface& other) = delete;

private:
	//save the upstream parent USB interface and the CDC channel struct
	USB_Interface& usb_if;
	CDC_Interface_Channel_t& cdc_channel;

	//internal variable to determine whether TX fifo is empty
	static const size_t TX_FIFO_SIZE = CFG_TUD_CDC_TX_BUFSIZE;

	//function that sends updated UART status
	void update_uart_status();
};


