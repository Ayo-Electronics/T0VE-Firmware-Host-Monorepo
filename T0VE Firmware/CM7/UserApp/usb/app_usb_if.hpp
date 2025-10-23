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
#include "app_vector.hpp"


class USB_Interface {
public:

	//===================== TYPEDEFS =======================
	//for this USB port, the hardware will just contain a list of descriptors,
	//i.e. device descriptors, interface descriptors, and string descriptors
	//right now, just supporting USB 2.0 FS, maybe support hi-speed in the future

	//################## DEVICE DESCRIPTORS ###################
	struct Device_Descriptor_t {
		//tinyusb calls just want to access a byte array
		std::array<uint8_t, sizeof(tusb_desc_device_t)> desc_device;

		//factory function that takes a tinyUSB array to build this
		//just basically a byte-by-byte copy of the structure
		static Device_Descriptor_t mk(tusb_desc_device_t _desc_device) {
			Device_Descriptor_t out; //output temporary

			//byte-by-byte memcpy from the configuration struct; return the output
			memcpy(out.desc_device.data(), &_desc_device, sizeof(_desc_device));
			return out;
		}
	};

	//################## CONFIGURATION DESCRIPTORS ###############
	struct Config_Descriptor_t {
		//use this variable to set the size of the descriptor
		static const size_t MAX_DESC_SIZE = 128;

		//just have a single growable container that contains descriptor bytes
		//increase the size of this as necessary
		App_Vector<uint8_t, MAX_DESC_SIZE> desc_configuration;

		//a little function to make the descriptor from a C-string
		template<size_t N>
		static Config_Descriptor_t mk(const uint8_t (&to_mk)[N]) {
			//sanity check the size
			static_assert(N <= MAX_DESC_SIZE, "Config descriptor initializer too large");

			//and actually make the descriptor
			Config_Descriptor_t out;
			out.add(to_mk);
			return out;
		}

		//a little function to add a descriptor
		template<size_t N>
		void add(const uint8_t (&to_add)[N]) {
			desc_configuration.push_n_back(to_add);
		}

		//and a little function to get the descriptors in a tinyUSB compatible format
		//just return a pointer to the underlying storage
		uint8_t* get() { return desc_configuration.data(); }
	};

	//################# STRING DESCRIPTORS ################
	struct String_Descriptor_t {
		//use this to change the max amount of string descriptors we can hold
		static const size_t MAX_NUM_DESC = 8;

		//store the string descriptors as a growable container of UTF-16 encoded strings
		App_Vector<App_Vector<uint16_t, 33>, MAX_NUM_DESC> desc_string;

		//#### have functions that edit the common descriptors ###
		void set_langID(uint16_t _langID = 0x0409) {	//default to english
			//grow the container to hold a language ID
			while(desc_string.size() < 1) desc_string.push_back({});

			//then reset our langID holder, and pop in our new langID
			desc_string[0].clear();
			desc_string[0].push_back((TUSB_DESC_STRING << 8) | (4)); //replicating tinyUSB behavior
			desc_string[0].push_back(_langID);
		}

		void set_manufacturer(App_String<32> mfg_string) 				{ write_string_index(mfg_string, 1); }
		void set_product(App_String<32> prod_string) 					{ write_string_index(prod_string, 2); }
		void set_serial(App_String<32> ser_string) 						{ write_string_index(ser_string, 3); }
		void set_interface(App_String<32> itf_string, size_t itf_index) { write_string_index(itf_string, itf_index + 4); }

		void write_string_index(App_String<32> to_write, size_t index) {
			//grow the container to hold the index
			while(desc_string.size() < (index + 1)) desc_string.push_back({});

			//then reset our index, and pop our new string in
			desc_string[index].clear();
			desc_string[index].push_back((TUSB_DESC_STRING << 8) | (2 * to_write.size() + 2)); 	//replicating tinyUSB behavior regarding size
			for(size_t i = 0; i < to_write.size(); i++) desc_string[index].push_back(to_write[i]);	//UTF16 low byte is just UTF8, can just pop in directly
		}

		//a factory function that takes an array of descriptors in english
		//the indices in these descriptors are shifted by 1, i.e. manufacturer should be passed in at position 0
		template<size_t N>
		static String_Descriptor_t mk(const App_String<32> (&to_mk)[N]) {
			//sanity check our descriptor size
			static_assert(N <= (MAX_NUM_DESC - 1), "String descriptor initializer too large");
			static_assert(N >= 3, "String descriptor must at least contain Manufacturer, Product, and Serial strings");

			String_Descriptor_t out;
			out.set_langID();				//default to english
			out.set_manufacturer(to_mk[0]);	//will always have manufacturer
			out.set_product(to_mk[1]);		//will always have product
			out.set_serial(to_mk[2]);		//will always have serial
			for(size_t i = 3; i < N; i++) {
				out.set_interface(to_mk[i], i - 3);	//and add interface descriptions if available
			}

			return out;	//return the output after we're done preparing it
		}

		//and have a method that gets the UTF16 encoded strings
		//directly return a pointer to the underlying buffer at the index
		uint16_t* get(uint8_t index) { return desc_string[index].data(); }
	};

	//and aggregate all these descriptors into a USB channel
	struct USB_Channel_t {
		Device_Descriptor_t	DEVICE_DESCRIPTORS;
		Config_Descriptor_t	CONFIG_DESCRIPTORS;
		String_Descriptor_t	STRING_DESCRIPTORS;
	};

	//have a single USB channel in our current firmware implementation
	static USB_Channel_t USB_CHANNEL;

	//===================== CONSTRUCTORS  ====================
	USB_Interface(USB_Channel_t& _usb_channel);

	//deleting copy constructor and assignment operator
	USB_Interface(const USB_Interface& other) = delete;
	void operator=(const USB_Interface& other) = delete;

	//=============== PUBLIC FUNCTIONS ===============
	//for now, just init
	void init();

	//editing the string descriptors
	void set_manufacturer(App_String<32> mfg_string);
	void set_product(App_String<32> prod_string);
	void set_serial(App_String<32> ser_string);
	void set_interface(App_String<32> itf_string, size_t itf_idx);

	//lets downstream interfaces request a soft-disconnect of the USB device
	//useful (in our case) to disconnect USB when we're running the high-speed computation
	//and reconnecting the interface afterward
	//TODO
	void connect_request() {}
	void disconnect_request() {}

private:
	//save the channel information for when we're preparing/editing our descriptors
	USB_Channel_t& usb_channel;

	//a scheduler just to run the tinyusb core
	Scheduler tud_task_scheduler;
};


