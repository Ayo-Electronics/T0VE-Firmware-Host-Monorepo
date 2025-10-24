/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021, Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This file is part of the TinyUSB stack.
 */

/* metadata:
   name: T0VE Processor Card
   url:
*/

#ifndef BOARD_H_
#define BOARD_H_

#ifdef __cplusplus
 extern "C" {
#endif

//for USB peripheral init
#include "usb_otg.h"

// VBUS Sense detection
#define OTG_FS_VBUS_SENSE     0
#define OTG_HS_VBUS_SENSE     0

// For this board does nothing
static inline void board_init2(void) {}

void board_vbus_set(uint8_t rhport, bool state) {
  (void) rhport; (void) state;
}

//IG: call CubeMX function for board init--clocking configured on startup
void board_init() {
	//call the MX usb device init
	MX_USB_OTG_FS_PCD_Init();
}

//IG: kinda hokey--can't use the C++ Tick API I put together, so just redirecting to HAL
uint32_t board_millis() {
	return HAL_GetTick();
}

#ifdef __cplusplus
 }
#endif

#endif
