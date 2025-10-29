/*
 * app_hal_timing.cpp
 *
 *  Created on: Mar 10, 2023
 *      Author: Ishaan
 *
 *	TIMER MAPPINGS:
 *
 */

#include "app_hal_tick.hpp"

extern "C" {
	#include "stm32h7xx_hal.h" //for HAL_Delay
}

//utility delay function
//should really never be called in the program if we write stuff well
//but useful for debugging
void Tick::delay_ms(uint32_t ms) {
	HAL_Delay(ms);
}

void Tick::delay_us(uint32_t us) {
	// Ensure DWT cycle counter is running; initialize the counter if not
	if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
		init_cycles();
		start_cycles();
	}

	//compute how many cycles we want to delay
	static const uint32_t cycles_per_us = CPU_FREQ_HZ / 1000000U;
	uint32_t target_cycles = us * cycles_per_us;
	uint32_t start = get_cycles();

	//stall until our microsecond timer elapses
	while ((get_cycles() - start) < target_cycles);
}



//system tick wrapper
uint32_t Tick::get_ms(){
	return HAL_GetTick();
}
