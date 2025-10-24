/*
 * app_usb_interface.cpp
 *
 *  Created on: Sep 17, 2025
 *      Author: govis
 */

#include "app_usb_interface.hpp"

//====================== MACROS =======================
// Helper macros for USB_OTG_FS endpoint access
//weird pointer arithmetic, but this is how they do it in the LL USB drivers
#define USB_FS_INEP(i)    ((USB_OTG_INEndpointTypeDef*)((uint32_t)USB_OTG_FS + USB_OTG_IN_ENDPOINT_BASE + ((i) * USB_OTG_EP_REG_SIZE)))
#define USB_FS_OUTEP(i)   ((USB_OTG_OUTEndpointTypeDef*)((uint32_t)USB_OTG_FS + USB_OTG_OUT_ENDPOINT_BASE + ((i) * USB_OTG_EP_REG_SIZE)))
#define USB_FS_DFIFO(i)   (*(__IO uint32_t*)((uint32_t)USB_OTG_FS + USB_OTG_FIFO_BASE + ((i) * USB_OTG_FIFO_SIZE)))

//====================== STATIC MEMBERS ======================

USB_Interface::USB_Hardware_Channel USB_Interface::USB_FS = {
		//USB handle that the HAL uses
		.usb_handle = &hpcd_USB_OTG_FS,

		//memory locations for USB-related registers
		.usb_global = USB_OTG_FS, //points to USB2 peripheral base
		.usb_device = (USB_OTG_DeviceTypeDef*)(USB2_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE), //some pointer fuckery, done by LL drivers

		//init and de-init functions
		.usb_init_function = Callback_Function<>(MX_USB_OTG_FS_PCD_Init),
		.usb_deinit_function = Callback_Function<>([](){HAL_PCD_MspDeInit(&hpcd_USB_OTG_FS);})
};

//======================= CONSTRUCTORS ======================

USB_Interface::USB_Interface(USB_Hardware_Channel& _usb_hw):
		usb_hw(_usb_hw)
{}

//======================= PUBLIC FUNCTIONS =======================

/*
 * Generally following the OTG_HS programming model
 * Section 60.15 in RM0399 (reference manual for STM32H747XI
 */
void USB_Interface::init() {
	//start by calling the HAL initialization function
	//just sets up clocking basically
	usb_hw.usb_init_function();

	//soft reset the entire USB core before proceeding with any initialization
	//soft_reset(); //NOTE: HAL init function already performs a core soft reset; also sets up peripheral power

	//following RM0399 initialization procedure
	core_init();
	device_init();
}


//====================== PRIVATE FUNCTIONS ======================
//##### INITIALIZATION
//take care of core initialization, RM0399 sec. 60.15.1
void USB_Interface::core_init() {
	usb_hw.usb_global->GAHBCFG |=
			USB_OTG_GAHBCFG_GINT | 			//enable interrupts from the USB core
			// USB_OTG_GAHBCFG_PTXFELVL |	//interrupt when the periodic TX fifo is completely empty, HOST MODE ONLY
			USB_OTG_GAHBCFG_TXFELVL; 		//interrupt when the nonperiodic TX fifo is completely empty

	usb_hw.usb_global->GINTSTS |=
			USB_OTG_GINTSTS_RXFLVL;			//interrupt when we need to read a byte from the RX fifo

	usb_hw.usb_global->GUSBCFG |=
			USB_OTG_GUSBCFG_FDMOD | 				//ISHAAN CUSTOM --> FORCE USB DEVICE MODE
			(0x0 << USB_OTG_GUSBCFG_TOCAL_Pos) |	//USB FS inter-packet timeout calibration; i think leave at default, change if necessary
			(0x9 << USB_OTG_GUSBCFG_TRDT_Pos);		//using default turnaround time

	usb_hw.usb_global->GINTMSK |=
			USB_OTG_GINTMSK_OTGINT |		//recommended to enable OTG interrupt
			USB_OTG_GINTMSK_MMISM;			//recommended to enable mode mismatch

}

//take care of device initialization, RM0399, sec. 60.15.3
void USB_Interface::device_init() {
	usb_hw.usb_device->DCFG |=
			(0x3 << USB_OTG_DCFG_DSPD_Pos) |	//full-speed using internal PHY
			USB_OTG_DCFG_NZLSOHSK;				//not 100% sure, how to deal with nonzero length status?

	usb_hw.usb_device->DCTL |= USB_OTG_DCTL_SDIS;	//generate a soft disconnect from the host; core issues connect

	usb_hw.usb_global->GINTMSK |=
			USB_OTG_GINTMSK_USBRST |	//also enable USB reset interrupt
			USB_OTG_GINTMSK_USBSUSPM |	//and USB suspend
			USB_OTG_GINTMSK_ENUMDNEM |	//and enumeration done
			USB_OTG_GINTMSK_ESUSPM | 	//and early suspend
			USB_OTG_GINTMSK_SOFM;		//and start of frame

	//wait for the USB reset, will show up as ISR (don't need to do anything other that wait I think)
	//	\--> maybe call `ep_init_usb_reset()`?
	//also wait for the enumeration to finish
	//	\--> read OTG_DSTS for enumeration speed
	//	\--> THEN do `ep_init_enum_cplt()`
	//can now accept SOF packets and perform control transfers on EP0
}

//##### ENDPOINT HELPERS
//after USB reset
void USB_Interface::ep_init_usb_reset() {
	//for all the OUT endpoints, set them up in NAK mode
	for(size_t i = 0; i < NUM_ENDPOINTS; i++) {
		USBx_OUTEP(i)->DOEPCTL |= USB_OTG_DOEPCTL_SNAK;
	}

	//unmasking relevant interrupt bits (across all endpoints)
	usb_hw.usb_device->DOEPMSK |=
			USB_OTG_DOEPMSK_STUPM |	//setup phase done
			USB_OTG_DOEPMSK_XFRCM;	//OUT transfer complete
	usb_hw.usb_device->DIEPMSK |=
			USB_OTG_DIEPMSK_XFRCM |	//IN transfer complete
			USB_OTG_DIEPMSK_TOM;	//timeout condition

	//setting up FIFO RAM according to our size allocations
	//global RX FIFO and EP0 IN TX FIFO
	usb_hw.usb_global->GRXFSIZ = RX_FIFO_SIZE;
	usb_hw.usb_global->DIEPTXF0_HNPTXFSIZ = (TX_FIFO_SIZE[0] << 16) |	//EP0 TX FIFO SIZE
											(RX_FIFO_SIZE << 0);		//EP0 TX FIFO start address (TODO: check to make sure this is in units of 32-bit words)

	//allow EP0 to receive 3-back to back OUT setup packets
	USBx_OUTEP(0)->DOEPTSIZ |= (3 << USB_OTG_DOEPTSIZ_STUPCNT_Pos);

	//TODO: figure out if I need to enable EP0 OUT and IN?
	//RESPONSE: once you set the setup count field in DOEPCTL0 register,
	//			the particular endpoint is fair game to receive SETUP packets irrespective
	//			of whether the EP is enabled or not (pg. 2906 of RM0399)
}

void USB_Interface::ep_init_enum_cplt() {
	//optionally check the OTG_DSTS register to check enumeration speed
	//program max packet size in DIEPCTL0 --> forcing this to 64 for FS
	USBx_INEP(0)->DIEPCTL |= (64 << USB_OTG_DIEPCTL_MPSIZ);
}

void USB_Interface::ep_init_set_address(uint8_t address) {
	//assuming that the `address` variable was set properly
	//going to write that value into the register
	usb_hw.usb_device->DCFG &= ~USB_OTG_DCFG_DAD_Msk; //clear address bits first
	usb_hw.usb_device->DCFG |= (address << USB_OTG_DCFG_DAD_Pos); //and write our new address

	//and send a `status_in` packet in response
	status_in();
}

void USB_Interface::ep_init_set_config_if(uint8_t cfg) {
	//our NORMAL configuration
	if(cfg == 1) {
		//TODO: go through our descriptors and set up the endpoints accordingly
		//call `activate()` on all active endpoints, `deactivate()` on inactive endpoints

		//set `configured` to `true`;
	}

	else {
		//`deactivate()` all endpoints except EP0 in/out

		//set `configured` to `false`
	}

}

//NOTES:
//may have to write `setup_word_done` to the core or something
