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
#define Buzzer_TIM2_Pin GPIO_PIN_5
#define Buzzer_TIM2_GPIO_Port GPIOA
#define Servo_TIM3_Pin GPIO_PIN_6
#define Servo_TIM3_GPIO_Port GPIOA
#define LED_Output_Pin GPIO_PIN_7
#define LED_Output_GPIO_Port GPIOA
#define Button_Input_Pin GPIO_PIN_4
#define Button_Input_GPIO_Port GPIOC
#define Stepper_Output_Pin GPIO_PIN_5
#define Stepper_Output_GPIO_Port GPIOC
#define Button_InputB1_Pin GPIO_PIN_1
#define Button_InputB1_GPIO_Port GPIOB
#define Photo_EXTI_Pin GPIO_PIN_2
#define Photo_EXTI_GPIO_Port GPIOB
#define Photo_EXTI_EXTI_IRQn EXTI2_IRQn
#define DHT_Output_Pin GPIO_PIN_10
#define DHT_Output_GPIO_Port GPIOB
#define Photo_EXTIB12_Pin GPIO_PIN_12
#define Photo_EXTIB12_GPIO_Port GPIOB
#define Photo_EXTIB12_EXTI_IRQn EXTI15_10_IRQn
#define Button_InputB13_Pin GPIO_PIN_13
#define Button_InputB13_GPIO_Port GPIOB
#define Button_InputB14_Pin GPIO_PIN_14
#define Button_InputB14_GPIO_Port GPIOB
#define Button_InputB15_Pin GPIO_PIN_15
#define Button_InputB15_GPIO_Port GPIOB
#define Stepper_OutputC6_Pin GPIO_PIN_6
#define Stepper_OutputC6_GPIO_Port GPIOC
#define LED_OutputC7_Pin GPIO_PIN_7
#define LED_OutputC7_GPIO_Port GPIOC
#define Stepper_OutputC8_Pin GPIO_PIN_8
#define Stepper_OutputC8_GPIO_Port GPIOC
#define FND_Output_Pin GPIO_PIN_9
#define FND_Output_GPIO_Port GPIOC
#define Button_EXTI_Pin GPIO_PIN_8
#define Button_EXTI_GPIO_Port GPIOA
#define Button_EXTI_EXTI_IRQn EXTI9_5_IRQn
#define Photo_EXTIA11_Pin GPIO_PIN_11
#define Photo_EXTIA11_GPIO_Port GPIOA
#define Photo_EXTIA11_EXTI_IRQn EXTI15_10_IRQn
#define Stepper_OutputA12_Pin GPIO_PIN_12
#define Stepper_OutputA12_GPIO_Port GPIOA
#define LED_OutputB6_Pin GPIO_PIN_6
#define LED_OutputB6_GPIO_Port GPIOB
#define FND_OutputB8_Pin GPIO_PIN_8
#define FND_OutputB8_GPIO_Port GPIOB
#define FND_OutputB9_Pin GPIO_PIN_9
#define FND_OutputB9_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
