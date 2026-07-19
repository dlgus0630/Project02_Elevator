/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body — Elevator Control
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "iwdg.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "app_elevator_fsm.h"
#include "dev_display.h"
#include "dev_photo.h"
#include "dev_stepper.h"
#include "dev_button.h"
#include "dev_servo.h"
#include "dev_buzzer.h"
#include "dev_dht.h"
#include "app_rs485_ctrl.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ── 시간 상수 (ms) ── */
#define DOOR_MOVE_MS   2000   // 서보가 0°↔180° 이동하는 데 걸리는 시간
#define BOARD_WAIT_MS  3000   // 문이 열린 뒤 탑승/하차 대기 시간
#define SETTLE_MS      3000   // 도착 후 관성 안정화 대기 시간
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile uint8_t  current_floor   = 1;   // 현재 층 (포토 ISR이 갱신)
volatile uint8_t  target_floor    = 1;   // 목표 층 (버튼이 설정)
volatile uint8_t  emergency_active = 0;  // 비상정지 플래그 (ISR이 세팅)
volatile uint8_t  inspection_active = 0;  // 점검 모드 플래그 (관리실 원격 명령으로 세팅/해제)
/* 출발·도착 시퀀스는 app_elevator_fsm.c의 4-state FSM으로 이관됨.
   pending_action/pending_tick/is_moving/emergency_announced는 제거되고
   FSM 내부 static 변수(s_state/s_door_phase/s_phase_tick)로 대체됨. */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_IWDG_Init();
  MX_TIM2_Init();
  MX_TIM5_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_Base_Start(&htim5);
  DHT_Init();
  Modbus_Init();
  Servo_Init();
  FND_Shift_Data(current_floor);
  Elevator_FSM_Init();               // FSM 상태 초기화 + Display_Init(RGB+LCD)
  printf("=== Elevator Start ===\r\n");

  uint32_t last_alive_tick = 0;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* 비상·버튼·4-state 전이·모터 스텝·상태표시(RGB+LCD)를 전부 처리 */
    Elevator_FSM_Update();

    /* ERROR 상태에서도 매 tick 계속 돌아야 하는 논블로킹 서비스들
       (비상 시에도 부저 엔벨로프/LED 점멸/FND/DHT/Modbus/워치독 유지) */
    LED_Bar_Update();
    Buzzer_Update();
    FND_Scan();
    DHT_Update();
    Modbus_Update();

    /* 1초 생존 신호 */
    if (HAL_GetTick() - last_alive_tick >= 1000)
    {
        printf("Alive floor=%d target=%d state=%d\r\n",
               current_floor, target_floor, (int)Elevator_FSM_GetState());
        last_alive_tick = HAL_GetTick();
    }

    /* 워치독 리프레시 — 모든 경로에서 매 tick 도달 */
    HAL_IWDG_Refresh(&hiwdg);

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 100;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* printf 리타겟 → USART2 (ESP8266 연동 전까지 디버그 출력용) */
int __io_putchar(int ch)
{
    if (ch == '\n')
        HAL_UART_Transmit(&huart2, (uint8_t *)"\r", 1, 0xFFFF);
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, 0xFFFF);
    return ch;
}

/* 포토센서 인터럽트 콜백 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_8)   // PA8 = 비상정지 버튼 (NVIC Priority 0)
    {
        if (!emergency_active)
        {
            emergency_active = 1;
            Motor_Stop();         // 모터 즉시 정지 — 안전 최우선
        }
        return;
    }

    /* 포토센서: current_floor 갱신 + 목표층 도달 시 Motor_Stop()만 수행.
       나머지 도착 후처리(LED·부저·문)는 메인루프 step 5에서 단일 처리 */
    Handle_Photo_Interrupt(GPIO_Pin);
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1) {}
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
