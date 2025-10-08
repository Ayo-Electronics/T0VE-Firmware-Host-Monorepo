/*
 * app_cdc_if.cpp
 *
 *  Created on: Oct 6, 2025
 *      Author: govis
 */

#include "app_cdc_if.hpp"

//========================== STATIC MEMBER INITIALIZATION =========================

CDC_Interface::CDC_Interface_Channel_t CDC_Interface::CDC_CHANNEL = {
		//interface number
		.cdc_itf_no = 0,

		//flow control status and line coding
		.flow_control = {{0}},
		.line_coding = {{0}},

		//callbacks
		.flow_control_change_cb = {},
		.coding_change_cb = {},
		.rx_available_cb = {},
};

//========================== PUBLIC FUNCTIONS ==========================

//### CONSTRUCTOR ###
//for now just save the interface channel
CDC_Interface::CDC_Interface(USB_Interface& _usb_if, CDC_Interface_Channel_t& _cdc_channel):
	usb_if(_usb_if), cdc_channel(_cdc_channel)
{}

void CDC_Interface::init() {
	//init upstream
	usb_if.init();

	//record the line coding
	cdc_line_coding_t coding;
	tud_cdc_n_get_line_coding(cdc_channel.cdc_itf_no, &coding);
	cdc_channel.line_coding.with([&](CDC_Line_Coding_t& _line_coding){
		_line_coding.baud_rate = coding.bit_rate;
		_line_coding.data_bits = coding.data_bits;
		_line_coding.parity = static_cast<Parity>(coding.parity);
		_line_coding.stop_bits = static_cast<Stop_Bits>(coding.stop_bits);
	});

	//and get the flow control status
	uint8_t line_state = tud_cdc_n_get_line_state(cdc_channel.cdc_itf_no);
	cdc_channel.flow_control.with([&](Flow_Control_t& _flow_control){
		_flow_control.dtr_status = line_state & (1 << 0); //DTR bit here
		_flow_control.rts_status = line_state & (1 << 1); //RTS bit here
	});
}

//############### CALLBACK REGISTERATION ###############
void CDC_Interface::register_flow_control_change_cb(Callback_Function<> cb) { cdc_channel.flow_control_change_cb = cb; }
void CDC_Interface::register_coding_change_cb(Callback_Function<> cb) { cdc_channel.coding_change_cb = cb; }
void CDC_Interface::register_rx_available_cb(Callback_Function<> cb) { cdc_channel.rx_available_cb = cb; }

//############### RECEIVE-SIDE FUNCTIONS ###############
size_t CDC_Interface::rx_bytes_available() {
	//return the bytes available for the particular channel
	return tud_cdc_n_available(cdc_channel.cdc_itf_no);
}

size_t CDC_Interface::rx_bytes_read(std::span<uint8_t, std::dynamic_extent> read_buffer) {
	//just forward to the tinyusb read function
	return tud_cdc_n_read(cdc_channel.cdc_itf_no, read_buffer.data(), read_buffer.size());
}

//############### TRANSMIT-SIDE FUNCTIONS ###############
size_t CDC_Interface::tx_bytes_write(std::span<uint8_t, std::dynamic_extent> transmit_buffer, bool immediate) {
	//output temporary
	size_t bytes_written;

	//forward to the tinyusb function
	bytes_written = tud_cdc_n_write(cdc_channel.cdc_itf_no, transmit_buffer.data(), transmit_buffer.size());

	//and if we wanna send the data immediately, flush the incomplete FIFO
	if(immediate) tud_cdc_n_write_flush(cdc_channel.cdc_itf_no);

	//and return our temporary
	return bytes_written;
}

size_t CDC_Interface::tx_bytes_available() {
	//forward to tinyusb
	return tud_cdc_n_write_available(cdc_channel.cdc_itf_no);
}

bool CDC_Interface::tx_fifo_empty() {
	//see if the number of bytes available is (greater than or) equal to our fifo size
	return (tx_bytes_available() >= TX_FIFO_SIZE);
}

//############### CONNECTION-RELATED FUNCTIONS ###############
//forward to tinyUSB
bool CDC_Interface::connected() { return tud_cdc_n_connected(cdc_channel.cdc_itf_no); }

void CDC_Interface::connect_request() {
	//call the upstream connect request
	usb_if.connect_request();

	//and say that we're present on the line
	//NOTE: not gonna set DSR status here--have the upstream explicitly call "set ready" afterward
	cdc_channel.flow_control.with([&](Flow_Control_t& _flow_control){
		_flow_control.dcd_status = true;
		_flow_control.dsr_status = false;
	});
	update_uart_status();
}

void CDC_Interface::disconnect_request() {
	//mark that we're "busy"; clear presence status
	//and update the host on this status
	cdc_channel.flow_control.with([&](Flow_Control_t& _flow_control){
		_flow_control.dcd_status = false;
		_flow_control.dsr_status = false;
	});
	update_uart_status();

	//clear the TX fifo
	tud_cdc_n_write_clear(cdc_channel.cdc_itf_no);

	//and call the upstream disconnect request
	usb_if.disconnect_request();
}

CDC_Interface::CDC_Line_Coding_t CDC_Interface::get_line_coding() {
	return cdc_channel.line_coding;
}

void CDC_Interface::set_ready() {
	cdc_channel.flow_control.with([&](Flow_Control_t& _flow_control){
		_flow_control.dsr_status = true;
	});
	update_uart_status();
}

void CDC_Interface::set_busy() {
	cdc_channel.flow_control.with([&](Flow_Control_t& _flow_control){
		_flow_control.dsr_status = false;
	});
	update_uart_status();
}


//====================================== PRIVATE FUNCTIONS =====================================
//update UART status based on the interface flags
void CDC_Interface::update_uart_status() {
	//structure we'll pass to tinyUSB
	cdc_notify_uart_state_t uart_state = {0};
	cdc_channel.flow_control.with([&](Flow_Control_t& _flow_control){
		uart_state.dcd = _flow_control.dcd_status;
		uart_state.dsr = _flow_control.dsr_status;
	});

	//actually send the state
	tud_cdc_n_notify_uart_state(cdc_channel.cdc_itf_no, &uart_state);
}

//################################### TINYUSB FORWARDING ###################################
//called when new RX data is available
void tud_cdc_rx_cb(uint8_t itf) {
	if(itf == CDC_Interface::CDC_CHANNEL.cdc_itf_no) {
		//run the rx available callback
		CDC_Interface::CDC_CHANNEL.rx_available_cb();
	}
}

//called when line states change
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
	if(itf == CDC_Interface::CDC_CHANNEL.cdc_itf_no) {
		//fill in the updated flow control information atomically
		bool last_dtr;
		CDC_Interface::CDC_CHANNEL.flow_control.with([&](CDC_Interface::Flow_Control_t& _flow_control){
			last_dtr = _flow_control.dtr_status;
			_flow_control.dtr_status = dtr;
			_flow_control.rts_status = rts;
		});

		//and notify that we have new flow control information (set our flag)
		CDC_Interface::CDC_CHANNEL.flow_control_change_cb();
	}
}

//called when line coding changes
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* p_line_coding) {
	if(itf == CDC_Interface::CDC_CHANNEL.cdc_itf_no) {
		//fill in the updated line coding information atomically
		CDC_Interface::CDC_CHANNEL.line_coding.with([&](CDC_Interface::CDC_Line_Coding_t& _line_coding){
			_line_coding.baud_rate = p_line_coding->bit_rate;;
			_line_coding.data_bits =  p_line_coding->data_bits;
			_line_coding.parity = static_cast<CDC_Interface::Parity>(p_line_coding->parity);
			_line_coding.stop_bits = static_cast<CDC_Interface::Stop_Bits>(p_line_coding->stop_bits);
		});

		//and notify that we have new line coding information (set our flag)
		CDC_Interface::CDC_CHANNEL.coding_change_cb();
	}
}

