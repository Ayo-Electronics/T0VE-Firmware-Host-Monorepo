/*
 * app_hal_gpio.cpp
 *
 *  Created on: Jun 9, 2025
 *      Author: govis
 */

#include "app_hal_gpio.hpp"

extern "C" {
	#include "gpio.h" //for HAL functions
}

//===================================== STATIC MEMBER DEFS ======================================
bool GPIO::all_init = false;

//========================================= CONSTRUCTOR ==========================================

GPIO::GPIO(const GPIO::GPIO_Hardware_Pin& _pin):
	pin(_pin),
	BSRR_REGISTER(reinterpret_cast<uint32_t>(_pin._GPIO_PORT) + 0x18), 	//BSRR offset from port register
	IDR_REGISTER(reinterpret_cast<uint32_t>(_pin._GPIO_PORT) + 0x10), 	//IDR offset from port register
	SET_MASK(pin._GPIO_PIN), 			//set is lower 16-bits
	CLEAR_MASK(pin._GPIO_PIN << 16),	//reset is upper 16-bits
	READ_MASK(pin._GPIO_PIN) 			//read is just the pin mask
{}

//========================================= INIT FUNCTIONS ==========================================

void GPIO::init() {
	//initialize the clocking and stuff for all GPIOs if we haven't done so
	init_clocking();

	//creating the initialization structure;
	GPIO_InitTypeDef init_struct = {0};
	init_struct.Speed = pin._GPIO_SPEED;
	init_struct.Pull = pin._GPIO_PULL;
	init_struct.Mode = pin._GPIO_MODE;
	init_struct.Pin = pin._GPIO_PIN;

	//actually initialize the pin
	HAL_GPIO_Init(pin._GPIO_PORT, &init_struct);
}

void GPIO::deinit() {
	//de-init via HAL function
	HAL_GPIO_DeInit(pin._GPIO_PORT,
					pin._GPIO_PIN);
}


//###### static methods ######

void GPIO::init_clocking() {
	//return if we've already initialized everything
	if(GPIO::all_init) return;

	//otherwise, initialize the clocking and stuff for all GPIOs if we haven't done so
	//just enable clocks to all GPIO ports, because why not
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_RCC_GPIOE_CLK_ENABLE();
	__HAL_RCC_GPIOF_CLK_ENABLE();
	__HAL_RCC_GPIOG_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();
	__HAL_RCC_GPIOI_CLK_ENABLE();
	__HAL_RCC_GPIOJ_CLK_ENABLE();
	__HAL_RCC_GPIOK_CLK_ENABLE();

	//need these two lines to be able to use PA0 and PA1
	//a little brute force-ish, but these are very edge-casey
	//HAL_SYSCFG_AnalogSwitchConfig(SYSCFG_SWITCH_PA0, SYSCFG_SWITCH_PA0_CLOSE); //don't think we need this line? I think PA0 should just be normally connected
	HAL_SYSCFG_AnalogSwitchConfig(SYSCFG_SWITCH_PA1, SYSCFG_SWITCH_PA1_CLOSE);

	GPIO::all_init = true;
}

//========================= ALTERNATE MODE FUNCTIONS =========================

GPIO_Alternate::GPIO_Alternate(const GPIO_Alternate_Hardware_Pin& _pin):
	GPIO(_pin._GPIO_INFO), //store the GPIO info in the parent class
	ALTERNATE_INDEX(_pin._ALTERNATE_INDEX), //store the alternate index in the child class
	ALTERNATE_MODE(_pin._ALTERNATE_MODE) //store the alternate mode in the child class	
{}

void GPIO_Alternate::configure_mode_gpio() {
	//just call the parent class init function
	GPIO::init();
}

void GPIO_Alternate::configure_mode_alternate() {
	//initialize the clocking and stuff for all GPIOs if we haven't done so
	//call the parent class init_clocking function
	GPIO::init_clocking();	

	//create the initialization structure
	GPIO_InitTypeDef init_struct = {0};

	//pin is inherited from the parent class, so we can use it to get the GPIO info
	init_struct.Speed = pin._GPIO_SPEED;
	init_struct.Pull = pin._GPIO_PULL;
	init_struct.Pin = pin._GPIO_PIN;

	//now pop in the alternate mode and index
	init_struct.Mode = ALTERNATE_MODE;
	init_struct.Alternate = ALTERNATE_INDEX;

	//actually initialize the pin
	HAL_GPIO_Init(pin._GPIO_PORT, &init_struct);
}
