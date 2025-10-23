/*
 * app_usb_if.cpp
 *
 *  Created on: Sep 18, 2025
 *      Author: govis
 */

#include "app_usb_if.hpp"

//================== DEFINES FOR TINYUSB DESCRIPTORS  ===================
//### DEVICE DESCRIPTORS ###
/* A combination of interfaces must have a unique product id, since PC will save device driver after the first plug.
 * Same VID/PID with different interface e.g MSC (first), then CDC (later) will possibly cause system error on PC.
 *
 * Auto ProductID layout's Bitmap:
 *   [MSB]         HID | MSC | CDC          [LSB]
 */
#define _PID_MAP(itf, n)  ( (CFG_TUD_##itf) << (n) )
#define USB_PID           (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) | \
                           _PID_MAP(MIDI, 3) | _PID_MAP(VENDOR, 4) )

#define USB_VID   0xCAFE
#define USB_BCD   0x0200

//### CONFIGURATION DESCRIPTORS ###
//edit this based on the interfaces we're supporting
enum {
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_MSC,
  ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82

#define EPNUM_MSC_OUT     0x03
#define EPNUM_MSC_IN      0x83

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)

//================== STATIC MEMBER DEFS ================

USB_Interface::USB_Channel_t USB_Interface::USB_CHANNEL = {
		.DEVICE_DESCRIPTORS = USB_Interface::Device_Descriptor_t::mk({
			    .bLength            = sizeof(tusb_desc_device_t),
			    .bDescriptorType    = TUSB_DESC_DEVICE,
			    .bcdUSB             = USB_BCD,

			    // Use Interface Association Descriptor (IAD) for CDC
			    // As required by USB Specs IAD's subclass must be common class (2) and protocol must be IAD (1)
			    .bDeviceClass       = TUSB_CLASS_MISC,
			    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
			    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
			    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

			    .idVendor           = USB_VID,
			    .idProduct          = USB_PID,
			    .bcdDevice          = 0x0100,

			    .iManufacturer      = 0x01,
			    .iProduct           = 0x02,
			    .iSerialNumber      = 0x03,

			    .bNumConfigurations = 0x01
			}),

		.CONFIG_DESCRIPTORS = USB_Interface::Config_Descriptor_t::mk({
			// Config number, interface count, string index, total length, attribute, power in mA
			TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

			// Interface number, string index, EP notification address and size, EP data address (out, in) and size.
			TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 16, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

			// Interface number, string index, EP Out & EP In address, EP size
			TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 5, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64)
		}),

		.STRING_DESCRIPTORS = USB_Interface::String_Descriptor_t::mk({
			"Ayo Electronics",
			"T0VE Processor Card",
			"[TODO_SERIAL_NUMBER]",
			"USB Serial Interface 1",
			"USB Mass Storage Interface 1",
		}),
};

//================== PUBLIC FUNCTIONS ==================

USB_Interface::USB_Interface(USB_Channel_t& _usb_channel): usb_channel(_usb_channel) {}


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

//### editing the string descriptors ###
void USB_Interface::set_manufacturer(App_String<32> mfg_string) {
	usb_channel.STRING_DESCRIPTORS.set_manufacturer(mfg_string);
}

void USB_Interface::set_product(App_String<32> prod_string) {
	usb_channel.STRING_DESCRIPTORS.set_product(prod_string);
}

void USB_Interface::set_serial(App_String<32> ser_string) {
	usb_channel.STRING_DESCRIPTORS.set_serial(ser_string);
}

void USB_Interface::set_interface(App_String<32> itf_string, size_t itf_idx) {
	usb_channel.STRING_DESCRIPTORS.set_interface(itf_string, itf_idx);
}

//================ TINYUSB DESCRIPTOR CALLBACKS ================

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb(void) {
	return USB_Interface::USB_CHANNEL.DEVICE_DESCRIPTORS.desc_device.data();
}

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
	(void) index; // for multiple configurations
#if TUD_OPT_HIGH_SPEED
	// Although we are highspeed, host may be fullspeed.
	return (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_hs_configuration : desc_fs_configuration;
#else
	return USB_Interface::USB_CHANNEL.CONFIG_DESCRIPTORS.desc_configuration.data();
#endif
}

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
	return USB_Interface::USB_CHANNEL.STRING_DESCRIPTORS.desc_string[index].data();
}
