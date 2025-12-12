/*
 * tusb_config.h
 *
 *  Created on: Sep 18, 2025
 *      Author: govis
 */

#pragma once

#ifdef __cplusplus
 extern "C" {
#endif

#define CFG_TUSB_MCU                OPT_MCU_STM32H7
#define CFG_TUSB_OS					OPT_OS_NONE
#define BOARD_DEVICE_RHPORT_SPEED   OPT_MODE_FULL_SPEED  // 12Mbps
#define BOARD_DEVICE_RHPORT_NUM     0
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

// In the case of the STM32H7 with an external HS 480 PHY, you must use root hub port 1 instead of 0
//    0 is for the internal FS 12mbit PHY so you'd use BOARD_DEVICE_RHPORT_NUM set to 0 and CFG_TUSB_RHPORT1_MODE set to (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)


//enable device-mode operation
#define CFG_TUD_ENABLED   1
#define CFG_TUH_ENABLED   0


/* USB DMA on some MCUs can only access a specific SRAM region with restriction on alignment.
* Tinyusb use follows macros to declare transferring memory so that they can be put
* into those specific section.
* e.g
* - CFG_TUSB_MEM SECTION : __attribute__ (( section(".usb_ram") ))
* - CFG_TUSB_MEM_ALIGN   : __attribute__ ((aligned(4)))
*
* IG: 	for STM32H7, DMA for USB should be able to access all memory
* 		revisit this section if we get any BusFaults, HardFaults, or MemManage execeptions
*/

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION		__attribute__((section(".FAST_SRAM_Section")))
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE   64
#endif

//------------- CLASS -------------//
#define CFG_TUD_CDC              1
#define CFG_TUD_MSC              0
#define CFG_TUD_HID              0
#define CFG_TUD_MIDI             0
#define CFG_TUD_VENDOR           0

#define CFG_TUD_CDC_NOTIFY        1 // Enable use of notification endpoint

// CDC FIFO size of TX and RX
#define CFG_TUD_CDC_RX_BUFSIZE   0x5000
#define CFG_TUD_CDC_TX_BUFSIZE   0x5000

// CDC Endpoint transfer buffer size, more is faster
#define CFG_TUD_CDC_EP_BUFSIZE   (TUD_OPT_HIGH_SPEED ? 512 : 64)

// MSC Buffer size of Device Mass storage, DEPRECATED
//IG: ALWAYS SET THIS TO TO A MULTIPLE OF THE SECTOR SIZE
//		\--> avoids unaligned accesses to file system sectors
//		\--> shouldn't have to deal with offsets when writing files
//#define CFG_TUD_MSC_EP_BUFSIZE	 512

#ifdef __cplusplus
 }
#endif
