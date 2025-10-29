/*
 * app_hal_hsem.cpp
 *
 *  Created on: Oct 24, 2025
 *      Author: govis
 */

#include "app_hal_hsem.hpp"

Hard_Semaphore::Hard_Semaphore(HSem_Channel channel):
	TAKE_REGISTER(&HSEM->RLR[channel]),
	READ_CLEAR_REGISTER(&HSEM->R[channel])
{}

void Hard_Semaphore::init() {
	//init clocking to the semaphore system
	__HAL_RCC_HSEM_CLK_ENABLE();

	//ask STM32 HAL to get the core ID to unlock semaphores
	CORE_ID = HAL_GetCurrentCPUID();
}


