/*
 * app_usb_endpoint.cpp
 *
 * Endpoint class implementations for STM32H747 OTG_FS device mode.
 * Written by Ishaan Gov with help from ChatGPT
 *
 * Usage examples with simplified constructors:
 *
 * // Create IN endpoint 1 for bulk transfers, 64 byte packets, 256 word FIFO
 * USB_In_Endpoint ep1_in(1, USB_EP_Packet_t::BULK, 64, 256);
 *
 * // Create OUT endpoint 2 for interrupt transfers, 32 byte packets
 * USB_Out_Endpoint ep2_out(2, USB_EP_Packet_t::INTERRUPT, 32);
 *
 * // Create control endpoint 0 with 512 word FIFO
 * USB_Zero_Endpoint ep0(512);
 */

#include "app_usb_endpoint.hpp"
#include <cstring>

// Helper macros for USB_OTG_FS endpoint access
#define USB_FS_INEP(i)    ((USB_OTG_INEndpointTypeDef*)((uint32_t)USB_OTG_FS + USB_OTG_IN_ENDPOINT_BASE + ((i) * USB_OTG_EP_REG_SIZE)))
#define USB_FS_OUTEP(i)   ((USB_OTG_OUTEndpointTypeDef*)((uint32_t)USB_OTG_FS + USB_OTG_OUT_ENDPOINT_BASE + ((i) * USB_OTG_EP_REG_SIZE)))
#define USB_FS_DFIFO(i)   (*(__IO uint32_t*)((uint32_t)USB_OTG_FS + USB_OTG_FIFO_BASE + ((i) * USB_OTG_FIFO_SIZE)))

// ----------------------------- IN endpoint -----------------------------

USB_In_Endpoint::USB_In_Endpoint(uint8_t ep_num,
                                 USB_EP_Packet_t packet_type,
                                 size_t packet_size,
                                 size_t fifo_words):
	USB_Endpoint(ep_num, USB_EP_Dir_t::IN, packet_type, packet_size),
	DIEPCTL_Register(	Register<uint32_t>(&USB_FS_INEP(ep_num)->DIEPCTL)	),
	DIEPINT_Register(	Register<uint32_t>(&USB_FS_INEP(ep_num)->DIEPINT)	),
	DIEPTSIZ_Register(	Register<uint32_t>(&USB_FS_INEP(ep_num)->DIEPTSIZ)	),
	DTXFSTS_Register(	Register<uint32_t>(&USB_FS_INEP(ep_num)->DTXFSTS)	),
	TX_FIFO(	Register<uint32_t>(&USB_FS_DFIFO(ep_num))	),
	TX_FIFO_SIZE(fifo_words)
{}

void USB_In_Endpoint::init() {
	//configure endpoint control register
    //TODO: revisit USBAEP, DPID (data PID), TXFNUM
    uint32_t ctl = (packet_size & USB_OTG_DIEPCTL_MPSIZ) //max packet size
                 | (static_cast<uint32_t>(packet_type) << USB_OTG_DIEPCTL_EPTYP_Pos) //endpoint type
				 | (ep_num << USB_OTG_DIEPCTL_TXFNUM_Pos)	//pick a FIFO corresponding to the endpoint number
				 | USB_OTG_DIEPCTL_USBAEP						//enable the endpoint
				 | USB_OTG_DIEPCTL_SD0PID_SEVNFRM 				//reset PID to DATA0, spec compliant
                 | USB_OTG_DIEPCTL_SNAK; 						//until we put data into the txfifo, nack all request
    DIEPCTL_Register = ctl;
}

size_t USB_In_Endpoint::write(std::span<const uint8_t> tx) {
	//if we pass an empty buffer, just return
    if (tx.empty()) return 0;

    //see if the transmit FIFO is empty--don't try to do anything if it isn't empty
    uint32_t free_words = DTXFSTS_Register & USB_OTG_DTXFSTS_INEPTFSAV;
    if (free_words != TX_FIFO_SIZE) return 0;

    //check how many bytes we wanna transmit, make sure that's no more than our max packet size
    //indicate the number of bytes we'll transfer, indicating that we'll transfer only a single packet
    const size_t num_bytes = clip(tx.size(), (size_t)0, packet_size);
    DIEPTSIZ_Register = (1u << USB_OTG_DIEPTSIZ_PKTCNT_Pos) | num_bytes;

    //copy the data we wanna send into our scratch buffer
    //doing this because we can only dump data into the FIFO with word (i.e. 4-byte) writes
    //have to do some funky reinterpret-casting to make this happen
    memset(scratch, 0, sizeof(scratch));
    std::memcpy(scratch, tx.data(), num_bytes);

    //and do the actual write--compute the number of words we need to write to our fifo and round up
    const uint32_t* w = reinterpret_cast<const uint32_t*>(scratch);
    const size_t num_words = (num_bytes + 3u) / 4u;
    for (size_t i=0; i < num_words; i++) TX_FIFO = w[i];

    //actually send packets on USB now, and enable the endpoint
    DIEPCTL_Register |=  USB_OTG_DIEPCTL_CNAK | USB_OTG_DIEPCTL_EPENA;

    //return the number of bytes we wrote
    return num_bytes;
}

bool USB_In_Endpoint::stall() {
	//set the stall bit in the endpoint control
    DIEPCTL_Register |= USB_OTG_DIEPCTL_STALL;
    return true;
}

bool USB_In_Endpoint::clear_stall() {
    //clear the stall bit, make sure we're DATA0 PID (spec requires upon exiting stall)
	DIEPCTL_Register &= ~(USB_OTG_DIEPCTL_STALL);
	DIEPCTL_Register |= USB_OTG_DIEPCTL_SD0PID_SEVNFRM;
    return true;
}

//void USB_In_Endpoint::reset() {
//    // Reset IN endpoint state: NAK, DATA0, clear size
//    DIEPCTL_Register |= USB_OTG_DIEPCTL_SNAK;
//    DIEPCTL_Register |= USB_OTG_DIEPCTL_SD0PID_SEVNFRM;
//    DIEPTSIZ_Register = 0;
//}

// ----------------------------- OUT endpoint -----------------------------

USB_Out_Endpoint::USB_Out_Endpoint(uint8_t ep_num,
                                   USB_EP_Packet_t packet_type,
                                   size_t packet_size)
: USB_Endpoint(ep_num, USB_EP_Dir_t::OUT, packet_type, packet_size),
  DOEPCTL_Register(		Register<uint32_t>(&USB_FS_OUTEP(ep_num)->DOEPCTL)	),
  DOEPINT_Register(		Register<uint32_t>(&USB_FS_OUTEP(ep_num)->DOEPINT)	),
  DOEPTSIZ_Register(		Register<uint32_t>(&USB_FS_OUTEP(ep_num)->DOEPTSIZ)	),
  RX_FIFO(		Register<uint32_t>(&USB_FS_DFIFO(ep_num))	)
{}

void USB_Out_Endpoint::init() {
    //set up our endpoint control register
    uint32_t ctl = (packet_size & USB_OTG_DOEPCTL_MPSIZ)	//maximum packet size
                 | (static_cast<uint32_t>(packet_type) << USB_OTG_DOEPCTL_EPTYP_Pos)	//endpoint type
				 | USB_OTG_DOEPCTL_USBAEP	//enable this endpoint
                 | USB_OTG_DOEPCTL_SNAK		//nack all OUT transactions for now until we're ready to receive
                 | USB_OTG_DOEPCTL_EPENA;	//and enable this endpoint
    DOEPCTL_Register = ctl;

    //set the endpoint up to receive data
    prime();
}

size_t USB_Out_Endpoint::available() const {
    return rx_bytes_available;
}

void USB_Out_Endpoint::mark_out_pending(size_t len) {
    rx_bytes_available = len;
}

//taking extra care to make sure this function is thread-safe
//locks rx_bytes_available during read
size_t USB_Out_Endpoint::read(std::span<uint8_t> rx) {
	//check if we have space to read information and if we have information to read
	//if not, return
    if (rx.empty() || rx_bytes_available == 0) return 0;

    //now do all of this atomically
    size_t to_copy;
    rx_bytes_available.with([&](size_t& _rx_bytes_available) {
		//figure out how much info we need to copy
    	to_copy = clip(rx.size(), (size_t)0, _rx_bytes_available);

		//then round this up to the nearest 4-byte boundary
		//since we can read the fifo in 4-byte chunks only
		//have to do some funky reinterpret casting in order to the FIFO reading happen
		uint32_t* scratch_u32 = reinterpret_cast<uint32_t*>(scratch);
		const size_t num_words = (_rx_bytes_available + 3u) / 4u;
		for (size_t i = 0; i < num_words; i++) scratch_u32[i] = RX_FIFO;

		//and now copy the scratch buffer to the receive buffer normally
		std::memcpy(rx.data(), scratch, to_copy);

		//we've read `to_copy` bytes, so decrement our out_pending_len
		_rx_bytes_available -= to_copy;

		//and if we've read the entire buffer out, we're ready to accept another packet
		if(!rx_bytes_available) prime();
    });

    //return the number of bytes we actually read
    return to_copy;
}

bool USB_Out_Endpoint::stall() {
    //set the stall bit
	DOEPCTL_Register |= USB_OTG_DOEPCTL_STALL;
    return true;
}

bool USB_Out_Endpoint::clear_stall() {
	//clear the stall bit, make sure we're DATA0 PID
	//set PID0 after stall
	DOEPCTL_Register &= ~(USB_OTG_DOEPCTL_STALL);
	DOEPCTL_Register |= USB_OTG_DOEPCTL_SD0PID_SEVNFRM;
	return true;
}

//#### IMPLEMENTING VIRTUAL FUNCTIONS #####
void USB_Out_Endpoint::on_reset() {
	//NACK any OUT transactions
	DOEPCTL_Register |= USB_OTG_DOEPCTL_SNAK;
}

//void USB_Out_Endpoint::reset() {
//    // Reset OUT endpoint state: NAK, DATA0, clear counters
//    DOEPCTL_Register |= USB_OTG_DOEPCTL_SNAK;
//    DOEPCTL_Register |= USB_OTG_DOEPCTL_SD0PID_SEVNFRM;
//    rx_bytes_available = 0;
//    prime();
//}

void USB_Out_Endpoint::prime() {
	//we're ready to read 1 packet of the max packet size
	//don't NACK OUT packets anymore and enable the endpoint
    DOEPTSIZ_Register = (1u << USB_OTG_DOEPTSIZ_PKTCNT_Pos) | packet_size;
    DOEPCTL_Register |= USB_OTG_DOEPCTL_CNAK | USB_OTG_DOEPCTL_EPENA;
}

// ----------------------------- EP0 -----------------------------

USB_Zero_Endpoint::USB_Zero_Endpoint(size_t fifo_words)
: USB_In_Endpoint(0, USB_EP_Packet_t::CONTROL, 64, fifo_words),
  USB_Out_Endpoint(0, USB_EP_Packet_t::CONTROL, 64)
{}

void USB_Zero_Endpoint::init() {
	//IN side can be initialized normally
	USB_In_Endpoint::init();

	// but OUT side has a special register
	// Special MPS encoding for control EP0
	uint32_t ep0_mps_code = 0; // default = 64
	switch (USB_Out_Endpoint::packet_size) {
		case 8:  ep0_mps_code = 0x3; break;
		case 16: ep0_mps_code = 0x2; break;
		case 32: ep0_mps_code = 0x1; break;
		case 64: ep0_mps_code = 0x0; break;
		default: ep0_mps_code = 0x0; break; // force 64 if invalid
	}

	uint32_t out_ctl =
		(ep0_mps_code & USB_OTG_DOEPCTL_MPSIZ) |
		(static_cast<uint32_t>(USB_Out_Endpoint::packet_type) << USB_OTG_DOEPCTL_EPTYP_Pos) |
		USB_OTG_DOEPCTL_USBAEP |
		USB_OTG_DOEPCTL_SNAK |
		USB_OTG_DOEPCTL_SD0PID_SEVNFRM;
	USB_Out_Endpoint::DOEPCTL_Register = out_ctl;

	USB_Out_Endpoint::prime();
}

size_t USB_Zero_Endpoint::send(std::span<const uint8_t> data) {
    //send data via the write port of the IN channel
	return USB_In_Endpoint::write(data);
}

size_t USB_Zero_Endpoint::expect(std::span<uint8_t> data) {
    return USB_Out_Endpoint::read(data);
}

void USB_Zero_Endpoint::status_in() {
	//write a zero-length packet output (bundles status at the protocol level)
    (void)USB_In_Endpoint::write({});
}

void USB_Zero_Endpoint::status_out() {
    //reads the status, basically a zero-length packet; provides information to the core
	uint8_t d;
    (void)USB_Out_Endpoint::read(std::span<uint8_t>(&d, 0));
}

bool USB_Zero_Endpoint::stall() {
	//stall the parents
    (void)USB_In_Endpoint::stall();
    (void)USB_Out_Endpoint::stall();
    return true;
}

bool USB_Zero_Endpoint::clear_stall() {
	//un-stall the parents
    (void)USB_In_Endpoint::clear_stall();
    (void)USB_Out_Endpoint::clear_stall();
    return true;
}

//####### IMPLEMENTING VIRTUAL FUNCTIONS #######
void USB_Zero_Endpoint::on_reset() {
	//do the `on_reset` functions as if it were a normal endpoint
	USB_In_Endpoint::on_reset();
	USB_Out_Endpoint::on_reset();

	//but also twiddle some registers related to EP0
	auto REGS = USB_In_Endpoint::DEV_REGS;
	REGS->DAINTMSK |= (1 << 0) | (1 << 16);	//unmask interrupts for EP0 in and out

	//accept up to SETUP packets in a row
	USB_FS_OUTEP(0)->DOEPTSIZ |= (0x3 << USB_OTG_DOEPTSIZ_STUPCNT_Pos);
}

//void USB_Zero_Endpoint::reset() {
//    // Reset both IN and OUT sides
//    USB_In_Endpoint::reset();
//    USB_Out_Endpoint::reset();
//}

