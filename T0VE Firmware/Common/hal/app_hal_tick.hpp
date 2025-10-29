/*
 * app_hal_timing.h
 *
 *  Created on: Mar 10, 2023
 *      Author: Ishaan
 *
 */

#pragma once

extern "C" {
#include "stm32h747xx.h"
}

#include "app_utils.hpp" //for CPU frequency

class Tick {
public:
	static void delay_ms(uint32_t ms);
	static void delay_us(uint32_t us);
	static uint32_t get_ms();

	//utilities for cycle counting--make these fast
	#pragma GCC push_options
	#pragma GCC optimize ("Ofast,unroll-loops,inline-functions")

	__attribute__((always_inline)) static void init_cycles() { CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; }
	__attribute__((always_inline)) static void reset_cycles() { DWT->CYCCNT = 0; }
	__attribute__((always_inline)) static uint32_t get_cycles() { return DWT->CYCCNT; }
	__attribute__((always_inline)) static void stop_cycles() { DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk; }
	__attribute__((always_inline)) static void start_cycles() { DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; }

	#pragma GCC pop_options

private:
	Tick(); //don't allow instantiation of a timer class just yet
};

