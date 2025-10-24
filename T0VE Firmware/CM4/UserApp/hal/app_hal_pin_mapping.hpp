/*
 * app_hal_pin_mapping.hpp
 *
 *  Created on: Jun 9, 2025
 *      Author: govis
 */

#pragma once

#include "app_hal_gpio.hpp"

class Pin_Mapping {
public:
	//=============================== STATIC MEMBERS ================================

	//=============================== LED ================================
	static const GPIO::GPIO_Hardware_Pin LED_RED;
	static const GPIO::GPIO_Hardware_Pin LED_GREEN;
	static const GPIO::GPIO_Hardware_Pin LED_BLUE;

	static const GPIO::GPIO_Hardware_Pin EXT_LED_GREEN;
	static const GPIO::GPIO_Hardware_Pin EXT_LED_YELLOW;

	//=============================== SOA + TIA CONTROL ================================
	static const GPIO::GPIO_Hardware_Pin SOA_EN_CH0;
	static const GPIO::GPIO_Hardware_Pin SOA_EN_CH1;
	static const GPIO::GPIO_Hardware_Pin SOA_EN_CH2;
	static const GPIO::GPIO_Hardware_Pin SOA_EN_CH3;

	static const GPIO::GPIO_Hardware_Pin TIA_EN_CH0;
	static const GPIO::GPIO_Hardware_Pin TIA_EN_CH1;
	static const GPIO::GPIO_Hardware_Pin TIA_EN_CH2;
	static const GPIO::GPIO_Hardware_Pin TIA_EN_CH3;

	//=============================== HIGH SPEED SPI CONTROL ================================
	static const GPIO_Alternate::GPIO_Alternate_Hardware_Pin SPI_CS_DAC_CH0;
	static const GPIO_Alternate::GPIO_Alternate_Hardware_Pin SPI_CS_ADC_CH0;
	static const GPIO_Alternate::GPIO_Alternate_Hardware_Pin SPI_CS_DAC_CH1;
	static const GPIO_Alternate::GPIO_Alternate_Hardware_Pin SPI_CS_ADC_CH1;
	static const GPIO_Alternate::GPIO_Alternate_Hardware_Pin SPI_CS_DAC_CH2;
	static const GPIO_Alternate::GPIO_Alternate_Hardware_Pin SPI_CS_ADC_CH2;
	static const GPIO_Alternate::GPIO_Alternate_Hardware_Pin SPI_CS_DAC_CH3;
	static const GPIO_Alternate::GPIO_Alternate_Hardware_Pin SPI_CS_ADC_CH3;

	//=============================== PWR CONTROL ================================
	static const GPIO::GPIO_Hardware_Pin PWR_REG_EN;
	static const GPIO::GPIO_Hardware_Pin PWR_PGOOD;

	static const GPIO::GPIO_Hardware_Pin EXT_PWR_REG_EN;
	static const GPIO::GPIO_Hardware_Pin EXT_PWR_PGOOD;

	//=============================== SYNC INTERFACE ================================
	static const GPIO::GPIO_Hardware_Pin SYNC_NODE_READY;
	static const GPIO::GPIO_Hardware_Pin SYNC_ALL_READY;
	static const GPIO::GPIO_Hardware_Pin SYNC_NID_0;
	static const GPIO::GPIO_Hardware_Pin SYNC_NID_1;
	static const GPIO::GPIO_Hardware_Pin SYNC_NID_2;
	static const GPIO::GPIO_Hardware_Pin SYNC_NID_3;

	//============================== WAVEGUIDE BIAS DAC ===============================
	static const GPIO::GPIO_Hardware_Pin BIAS_DRIVE_EN;
	static const GPIO::GPIO_Hardware_Pin BIAS_DAC_RESET;

	//=============================== MISCELLANEOUS PINS ===============================
	static const GPIO::GPIO_Hardware_Pin PRES_INTLK; //on aux card,
	static const GPIO::GPIO_Hardware_Pin PD_SEL_PIC; //on aux card,
	static const GPIO::GPIO_Hardware_Pin PD_SEL_AUX; //on aux card
};

