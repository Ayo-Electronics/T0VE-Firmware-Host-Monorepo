/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SPI_MISO_CH2_Pin GPIO_PIN_9
#define SPI_MISO_CH2_GPIO_Port GPIOG
#define DRAM_HIGHBYTE_Pin GPIO_PIN_1
#define DRAM_HIGHBYTE_GPIO_Port GPIOE
#define CS_ADC_CH3_Pin GPIO_PIN_4
#define CS_ADC_CH3_GPIO_Port GPIOB
#define DRAM_LOWBYTE_Pin GPIO_PIN_0
#define DRAM_LOWBYTE_GPIO_Port GPIOE
#define SPI_SCK_CH2_Pin GPIO_PIN_3
#define SPI_SCK_CH2_GPIO_Port GPIOB
#define SPI_MISO_CH0_Pin GPIO_PIN_12
#define SPI_MISO_CH0_GPIO_Port GPIOG
#define SPI_MOSI_CH2_Pin GPIO_PIN_7
#define SPI_MOSI_CH2_GPIO_Port GPIOD
#define DRAM_nCAS_Pin GPIO_PIN_15
#define DRAM_nCAS_GPIO_Port GPIOG
#define SPI_MOSI_CH0_Pin GPIO_PIN_14
#define SPI_MOSI_CH0_GPIO_Port GPIOG
#define SPI_SCK_CH0_Pin GPIO_PIN_13
#define SPI_SCK_CH0_GPIO_Port GPIOG
#define DRAM_D3_Pin GPIO_PIN_0
#define DRAM_D3_GPIO_Port GPIOD
#define DRAM_D2_Pin GPIO_PIN_1
#define DRAM_D2_GPIO_Port GPIOD
#define CS_ADC_CH0_Pin GPIO_PIN_7
#define CS_ADC_CH0_GPIO_Port GPIOC
#define DRAM_CLK_Pin GPIO_PIN_8
#define DRAM_CLK_GPIO_Port GPIOG
#define DRAM_A2_Pin GPIO_PIN_2
#define DRAM_A2_GPIO_Port GPIOF
#define DRAM_A1_Pin GPIO_PIN_1
#define DRAM_A1_GPIO_Port GPIOF
#define DRAM_A0_Pin GPIO_PIN_0
#define DRAM_A0_GPIO_Port GPIOF
#define DRAM_BANK1_Pin GPIO_PIN_5
#define DRAM_BANK1_GPIO_Port GPIOG
#define DRAM_A3_Pin GPIO_PIN_3
#define DRAM_A3_GPIO_Port GPIOF
#define DRAM_BANK0_Pin GPIO_PIN_4
#define DRAM_BANK0_GPIO_Port GPIOG
#define DRAM_A5_Pin GPIO_PIN_5
#define DRAM_A5_GPIO_Port GPIOF
#define DRAM_A4_Pin GPIO_PIN_4
#define DRAM_A4_GPIO_Port GPIOF
#define SYNC_SYNC_IN_Pin GPIO_PIN_1
#define SYNC_SYNC_IN_GPIO_Port GPIOK
#define SPI_MISO_CH3_Pin GPIO_PIN_11
#define SPI_MISO_CH3_GPIO_Port GPIOJ
#define SPI_MOSI_CH3_Pin GPIO_PIN_10
#define SPI_MOSI_CH3_GPIO_Port GPIOJ
#define SPI_MISO_CH1_Pin GPIO_PIN_2
#define SPI_MISO_CH1_GPIO_Port GPIOC
#define SPI_MOSI_CH1_Pin GPIO_PIN_3
#define SPI_MOSI_CH1_GPIO_Port GPIOC
#define DRAM_CKE_Pin GPIO_PIN_2
#define DRAM_CKE_GPIO_Port GPIOH
#define CS_DAC_CH3_Pin GPIO_PIN_2
#define CS_DAC_CH3_GPIO_Port GPIOA
#define CS_DAC_CH2_Pin GPIO_PIN_0
#define CS_DAC_CH2_GPIO_Port GPIOA
#define DRAM_D7_Pin GPIO_PIN_10
#define DRAM_D7_GPIO_Port GPIOE
#define DRAM_nCS_Pin GPIO_PIN_3
#define DRAM_nCS_GPIO_Port GPIOH
#define DRAM_WE_Pin GPIO_PIN_5
#define DRAM_WE_GPIO_Port GPIOH
#define DRAM_A7_Pin GPIO_PIN_13
#define DRAM_A7_GPIO_Port GPIOF
#define DRAM_A8_Pin GPIO_PIN_14
#define DRAM_A8_GPIO_Port GPIOF
#define DRAM_D6_Pin GPIO_PIN_9
#define DRAM_D6_GPIO_Port GPIOE
#define DRAM_D8_Pin GPIO_PIN_11
#define DRAM_D8_GPIO_Port GPIOE
#define CS_DAC_CH0_Pin GPIO_PIN_11
#define CS_DAC_CH0_GPIO_Port GPIOB
#define DRAM_D1_Pin GPIO_PIN_15
#define DRAM_D1_GPIO_Port GPIOD
#define DRAM_D0_Pin GPIO_PIN_14
#define DRAM_D0_GPIO_Port GPIOD
#define DRAM_A6_Pin GPIO_PIN_12
#define DRAM_A6_GPIO_Port GPIOF
#define DRAM_A9_Pin GPIO_PIN_15
#define DRAM_A9_GPIO_Port GPIOF
#define DRAM_D9_Pin GPIO_PIN_12
#define DRAM_D9_GPIO_Port GPIOE
#define DRAM_D12_Pin GPIO_PIN_15
#define DRAM_D12_GPIO_Port GPIOE
#define CS_DAC_CH1_Pin GPIO_PIN_1
#define CS_DAC_CH1_GPIO_Port GPIOA
#define SYNC_SYNC_OUT_Pin GPIO_PIN_5
#define SYNC_SYNC_OUT_GPIO_Port GPIOA
#define CS_ADC_CH1_Pin GPIO_PIN_1
#define CS_ADC_CH1_GPIO_Port GPIOB
#define DRAM_nRAS_Pin GPIO_PIN_11
#define DRAM_nRAS_GPIO_Port GPIOF
#define DRAM_A10_Pin GPIO_PIN_0
#define DRAM_A10_GPIO_Port GPIOG
#define DRAM_D5_Pin GPIO_PIN_8
#define DRAM_D5_GPIO_Port GPIOE
#define DRAM_D10_Pin GPIO_PIN_13
#define DRAM_D10_GPIO_Port GPIOE
#define SPI_SCK_CH3_Pin GPIO_PIN_6
#define SPI_SCK_CH3_GPIO_Port GPIOH
#define DRAM_D15_Pin GPIO_PIN_10
#define DRAM_D15_GPIO_Port GPIOD
#define DRAM_D14_Pin GPIO_PIN_9
#define DRAM_D14_GPIO_Port GPIOD
#define CS_ADC_CH2_Pin GPIO_PIN_0
#define CS_ADC_CH2_GPIO_Port GPIOB
#define DRAM_A11_Pin GPIO_PIN_1
#define DRAM_A11_GPIO_Port GPIOG
#define DRAM_D4_Pin GPIO_PIN_7
#define DRAM_D4_GPIO_Port GPIOE
#define DRAM_D11_Pin GPIO_PIN_14
#define DRAM_D11_GPIO_Port GPIOE
#define SPI_SCK_CH1_Pin GPIO_PIN_13
#define SPI_SCK_CH1_GPIO_Port GPIOB
#define DRAM_D8D8_Pin GPIO_PIN_8
#define DRAM_D8D8_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */
#define BIAS_DRIVE_EN_Pin GPIO_PIN_10
#define BIAS_DRIVE_EN_GPIO_Port GPIOG
#define SPI_MISO_CH2_Pin GPIO_PIN_9
#define SPI_MISO_CH2_GPIO_Port GPIOG
#define DRAM_HIGHBYTE_Pin GPIO_PIN_1
#define DRAM_HIGHBYTE_GPIO_Port GPIOE
#define CS_ADC_CH3_Pin GPIO_PIN_4
#define CS_ADC_CH3_GPIO_Port GPIOB
#define SOA_EN_CH1_Pin GPIO_PIN_15
#define SOA_EN_CH1_GPIO_Port GPIOJ
#define EXT_PWR_PGOOD_Pin GPIO_PIN_15
#define EXT_PWR_PGOOD_GPIO_Port GPIOH
#define DRAM_LOWBYTE_Pin GPIO_PIN_0
#define DRAM_LOWBYTE_GPIO_Port GPIOE
#define SPI_SCK_CH2_Pin GPIO_PIN_3
#define SPI_SCK_CH2_GPIO_Port GPIOB
#define TIA_EN_CH0_Pin GPIO_PIN_3
#define TIA_EN_CH0_GPIO_Port GPIOK
#define SPI_MISO_CH0_Pin GPIO_PIN_12
#define SPI_MISO_CH0_GPIO_Port GPIOG
#define SPI_MOSI_CH2_Pin GPIO_PIN_7
#define SPI_MOSI_CH2_GPIO_Port GPIOD
#define PD_SEL_AUX_Pin GPIO_PIN_5
#define PD_SEL_AUX_GPIO_Port GPIOE
#define LED_BLUE_Pin GPIO_PIN_3
#define LED_BLUE_GPIO_Port GPIOE
#define DRAM_nCAS_Pin GPIO_PIN_15
#define DRAM_nCAS_GPIO_Port GPIOG
#define SPI_MOSI_CH0_Pin GPIO_PIN_14
#define SPI_MOSI_CH0_GPIO_Port GPIOG
#define SPI_SCK_CH0_Pin GPIO_PIN_13
#define SPI_SCK_CH0_GPIO_Port GPIOG
#define TIA_EN_CH2_Pin GPIO_PIN_14
#define TIA_EN_CH2_GPIO_Port GPIOJ
#define SOA_EN_CH3_Pin GPIO_PIN_12
#define SOA_EN_CH3_GPIO_Port GPIOJ
#define DRAM_D3_Pin GPIO_PIN_0
#define DRAM_D3_GPIO_Port GPIOD
#define PRES_INTLK_Pin GPIO_PIN_13
#define PRES_INTLK_GPIO_Port GPIOH
#define LED_GREEN_Pin GPIO_PIN_13
#define LED_GREEN_GPIO_Port GPIOJ
#define DRAM_D2_Pin GPIO_PIN_1
#define DRAM_D2_GPIO_Port GPIOD
#define SYNC_NODE_RDY_Pin GPIO_PIN_11
#define SYNC_NODE_RDY_GPIO_Port GPIOI
#define CS_ADC_CH0_Pin GPIO_PIN_7
#define CS_ADC_CH0_GPIO_Port GPIOC
#define DRAM_CLK_Pin GPIO_PIN_8
#define DRAM_CLK_GPIO_Port GPIOG
#define PD_SEL_PIC_Pin GPIO_PIN_7
#define PD_SEL_PIC_GPIO_Port GPIOG
#define DRAM_A2_Pin GPIO_PIN_2
#define DRAM_A2_GPIO_Port GPIOF
#define DRAM_A1_Pin GPIO_PIN_1
#define DRAM_A1_GPIO_Port GPIOF
#define DRAM_A0_Pin GPIO_PIN_0
#define DRAM_A0_GPIO_Port GPIOF
#define DRAM_BANK1_Pin GPIO_PIN_5
#define DRAM_BANK1_GPIO_Port GPIOG
#define LED_RED_Pin GPIO_PIN_12
#define LED_RED_GPIO_Port GPIOI
#define BIAS_DAC_RESET_Pin GPIO_PIN_13
#define BIAS_DAC_RESET_GPIO_Port GPIOI
#define DRAM_A3_Pin GPIO_PIN_3
#define DRAM_A3_GPIO_Port GPIOF
#define DRAM_BANK0_Pin GPIO_PIN_4
#define DRAM_BANK0_GPIO_Port GPIOG
#define SYNC_ALL_RDY_Pin GPIO_PIN_2
#define SYNC_ALL_RDY_GPIO_Port GPIOK
#define DRAM_A5_Pin GPIO_PIN_5
#define DRAM_A5_GPIO_Port GPIOF
#define DRAM_A4_Pin GPIO_PIN_4
#define DRAM_A4_GPIO_Port GPIOF
#define EXT_LED_YELLOW_Pin GPIO_PIN_0
#define EXT_LED_YELLOW_GPIO_Port GPIOK
#define SYNC_SYNC_IN_Pin GPIO_PIN_1
#define SYNC_SYNC_IN_GPIO_Port GPIOK
#define SPI_MISO_CH3_Pin GPIO_PIN_11
#define SPI_MISO_CH3_GPIO_Port GPIOJ
#define PGOOD_Pin GPIO_PIN_0
#define PGOOD_GPIO_Port GPIOC
#define SPI_MOSI_CH3_Pin GPIO_PIN_10
#define SPI_MOSI_CH3_GPIO_Port GPIOJ
#define SPI_MISO_CH1_Pin GPIO_PIN_2
#define SPI_MISO_CH1_GPIO_Port GPIOC
#define SPI_MOSI_CH1_Pin GPIO_PIN_3
#define SPI_MOSI_CH1_GPIO_Port GPIOC
#define DRAM_CKE_Pin GPIO_PIN_2
#define DRAM_CKE_GPIO_Port GPIOH
#define CS_DAC_CH3_Pin GPIO_PIN_2
#define CS_DAC_CH3_GPIO_Port GPIOA
#define CS_DAC_CH2_Pin GPIO_PIN_0
#define CS_DAC_CH2_GPIO_Port GPIOA
#define TIA_EN_CH3_Pin GPIO_PIN_0
#define TIA_EN_CH3_GPIO_Port GPIOJ
#define DRAM_D7_Pin GPIO_PIN_10
#define DRAM_D7_GPIO_Port GPIOE
#define SYNC_NID1_Pin GPIO_PIN_8
#define SYNC_NID1_GPIO_Port GPIOJ
#define DRAM_nCS_Pin GPIO_PIN_3
#define DRAM_nCS_GPIO_Port GPIOH
#define EXT_PWR_REG_EN_Pin GPIO_PIN_4
#define EXT_PWR_REG_EN_GPIO_Port GPIOH
#define DRAM_WE_Pin GPIO_PIN_5
#define DRAM_WE_GPIO_Port GPIOH
#define SOA_EN_CH2_Pin GPIO_PIN_1
#define SOA_EN_CH2_GPIO_Port GPIOJ
#define DRAM_A7_Pin GPIO_PIN_13
#define DRAM_A7_GPIO_Port GPIOF
#define DRAM_A8_Pin GPIO_PIN_14
#define DRAM_A8_GPIO_Port GPIOF
#define DRAM_D6_Pin GPIO_PIN_9
#define DRAM_D6_GPIO_Port GPIOE
#define DRAM_D8_Pin GPIO_PIN_11
#define DRAM_D8_GPIO_Port GPIOE
#define CS_DAC_CH0_Pin GPIO_PIN_11
#define CS_DAC_CH0_GPIO_Port GPIOB
#define DRAM_D1_Pin GPIO_PIN_15
#define DRAM_D1_GPIO_Port GPIOD
#define DRAM_D0_Pin GPIO_PIN_14
#define DRAM_D0_GPIO_Port GPIOD
#define SYNC_NID2_Pin GPIO_PIN_7
#define SYNC_NID2_GPIO_Port GPIOA
#define EXT_LED_GREEN_Pin GPIO_PIN_2
#define EXT_LED_GREEN_GPIO_Port GPIOB
#define DRAM_A6_Pin GPIO_PIN_12
#define DRAM_A6_GPIO_Port GPIOF
#define DRAM_A9_Pin GPIO_PIN_15
#define DRAM_A9_GPIO_Port GPIOF
#define DRAM_D9_Pin GPIO_PIN_12
#define DRAM_D9_GPIO_Port GPIOE
#define DRAM_D12_Pin GPIO_PIN_15
#define DRAM_D12_GPIO_Port GPIOE
#define SYNC_NID3_Pin GPIO_PIN_13
#define SYNC_NID3_GPIO_Port GPIOD
#define CS_DAC_CH1_Pin GPIO_PIN_1
#define CS_DAC_CH1_GPIO_Port GPIOA
#define SYNC_SYNC_OUT_Pin GPIO_PIN_5
#define SYNC_SYNC_OUT_GPIO_Port GPIOA
#define REG_EN_Pin GPIO_PIN_4
#define REG_EN_GPIO_Port GPIOC
#define CS_ADC_CH1_Pin GPIO_PIN_1
#define CS_ADC_CH1_GPIO_Port GPIOB
#define TIA_EN_CH1_Pin GPIO_PIN_2
#define TIA_EN_CH1_GPIO_Port GPIOJ
#define DRAM_nRAS_Pin GPIO_PIN_11
#define DRAM_nRAS_GPIO_Port GPIOF
#define DRAM_A10_Pin GPIO_PIN_0
#define DRAM_A10_GPIO_Port GPIOG
#define DRAM_D5_Pin GPIO_PIN_8
#define DRAM_D5_GPIO_Port GPIOE
#define DRAM_D10_Pin GPIO_PIN_13
#define DRAM_D10_GPIO_Port GPIOE
#define SPI_SCK_CH3_Pin GPIO_PIN_6
#define SPI_SCK_CH3_GPIO_Port GPIOH
#define DRAM_D15_Pin GPIO_PIN_10
#define DRAM_D15_GPIO_Port GPIOD
#define DRAM_D14_Pin GPIO_PIN_9
#define DRAM_D14_GPIO_Port GPIOD
#define SYNC_NID0_Pin GPIO_PIN_3
#define SYNC_NID0_GPIO_Port GPIOA
#define CS_ADC_CH2_Pin GPIO_PIN_0
#define CS_ADC_CH2_GPIO_Port GPIOB
#define SOA_EN_CH0_Pin GPIO_PIN_3
#define SOA_EN_CH0_GPIO_Port GPIOJ
#define DRAM_A11_Pin GPIO_PIN_1
#define DRAM_A11_GPIO_Port GPIOG
#define DRAM_D4_Pin GPIO_PIN_7
#define DRAM_D4_GPIO_Port GPIOE
#define DRAM_D11_Pin GPIO_PIN_14
#define DRAM_D11_GPIO_Port GPIOE
#define SPI_SCK_CH1_Pin GPIO_PIN_13
#define SPI_SCK_CH1_GPIO_Port GPIOB
#define DRAM_D8D8_Pin GPIO_PIN_8
#define DRAM_D8D8_GPIO_Port GPIOD

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
