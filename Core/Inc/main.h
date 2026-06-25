/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "stm32f4xx_hal.h"

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
#define DISP_DC_Pin GPIO_PIN_2
#define DISP_DC_GPIO_Port GPIOE
#define DISP_RST_Pin GPIO_PIN_3
#define DISP_RST_GPIO_Port GPIOE
#define DISP_CS0_Pin GPIO_PIN_4
#define DISP_CS0_GPIO_Port GPIOE
#define DISP_CS1_Pin GPIO_PIN_5
#define DISP_CS1_GPIO_Port GPIOE
#define DISP_CS2_Pin GPIO_PIN_6
#define DISP_CS2_GPIO_Port GPIOE
#define LED_Pin GPIO_PIN_13
#define LED_GPIO_Port GPIOC
#define PLAT_INDEX_Pin GPIO_PIN_2
#define PLAT_INDEX_GPIO_Port GPIOA
#define PLAT_INDEX_EXTI_IRQn EXTI2_IRQn
#define MT_CS_Pin GPIO_PIN_4
#define MT_CS_GPIO_Port GPIOA
#define ARM_EN_Pin GPIO_PIN_4
#define ARM_EN_GPIO_Port GPIOC
#define ARM_FAULT_Pin GPIO_PIN_5
#define ARM_FAULT_GPIO_Port GPIOC
#define ARM_HOME_Pin GPIO_PIN_7
#define ARM_HOME_GPIO_Port GPIOE
#define ARM_HOME_EXTI_IRQn EXTI9_5_IRQn
#define KEY0_Pin GPIO_PIN_8
#define KEY0_GPIO_Port GPIOE
#define KEY1_Pin GPIO_PIN_9
#define KEY1_GPIO_Port GPIOE
#define KEY2_Pin GPIO_PIN_10
#define KEY2_GPIO_Port GPIOE
#define AS_CS_Pin GPIO_PIN_12
#define AS_CS_GPIO_Port GPIOB
#define PLAT_EN_Pin GPIO_PIN_13
#define PLAT_EN_GPIO_Port GPIOB
#define PLAT_FAULT_Pin GPIO_PIN_14
#define PLAT_FAULT_GPIO_Port GPIOB
#define DISP_BL_Pin GPIO_PIN_12
#define DISP_BL_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
