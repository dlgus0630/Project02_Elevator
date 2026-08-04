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

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* 아래 전역들은 ISR과 메인루프가 함께 읽고 쓰므로 전부 volatile.
   App/BSP 레이어에서 extern으로 참조한다. */
volatile uint8_t  current_floor   = 1;   // 현재 층 — 포토센서 ISR이 갱신
volatile uint8_t  target_floor    = 1;   // 목표 층 — 층 버튼 및 RS-485 원격 명령이 설정
volatile uint8_t  emergency_active = 0;  // 비상정지(E401) 플래그 — PA8 EXTI ISR이 세팅
volatile uint8_t  inspection_active = 0; // 점검모드(E402) 플래그 — 관제실 RS-485 명령으로 세팅/해제
volatile uint8_t  move_timeout_active = 0;   // 이동 타임아웃(E201) 플래그 — FSM이 세팅, 관제실 리셋으로 해제
volatile uint8_t  floor_skip_active   = 0;   // 층 건너뜀(E301) 플래그 — FSM이 세팅, 관제실 리셋으로 해제
volatile uint8_t  floor2_transit_flag = 0;   // 이번 이동 중 2층 센서를 지나쳤는지 — 1<->3층 이동의 E301 판정용
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
  Elevator_FSM_Init();               // FSM 초기화 + Display_Init(RGB+LCD) + 부팅 시 실제 층 감지(호밍)
  printf("=== Elevator Start ===\r\n");

  uint32_t last_alive_tick = 0;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* 비상·점검 감지, 버튼 처리, 5-state(IDLE/MOVING/DOOR_OPEN/ERROR/INSPECTION) 전이,
       모터 스텝, 상태표시(RGB+LCD)를 한 번에 처리 */
    Elevator_FSM_Update();

    /* ERROR/INSPECTION 상태에서도 매 tick 계속 돌아야 하는 논블로킹 서비스들.
       (비상 중에도 부저 엔벨로프·LED 점멸·FND 스캔·DHT 측정·Modbus 응답이 멈추면 안 됨) */
    LED_Bar_Update();
    Buzzer_Update();
    FND_Scan();
    DHT_Update();
    Modbus_Update();

    /* 1초 주기 생존 로그 — 현재층·목표층·FSM 상태를 UART로 확인 */
    if (HAL_GetTick() - last_alive_tick >= 1000)
    {
        printf("Alive floor=%d target=%d state=%d\r\n",
               current_floor, target_floor, (int)Elevator_FSM_GetState());
        last_alive_tick = HAL_GetTick();
    }

    /* IWDG 리프레시 — 루프 맨 끝에 두어 위쪽 서비스가 전부 끝난 뒤에만 도달하게 한다.
       중간에서 리프레시하면 블로킹 I2C(LCD) 등이 멎어도 워치독이 리셋을 못 건다. */
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

/* printf 리타겟 → USART2 디버그 출력. USART1은 RS-485 통신 전용이라 쓰지 않는다. */
int __io_putchar(int ch)
{
    if (ch == '\n')
        HAL_UART_Transmit(&huart2, (uint8_t *)"\r", 1, 0xFFFF);
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, 0xFFFF);
    return ch;
}

/* EXTI 공통 콜백 — 비상정지 버튼(PA8)과 층 포토센서(PA11/PB12/PB2)가 함께 들어온다. */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_8)   // PA8 = 비상정지 버튼. EXTI9_5 우선순위 0으로 포토센서(1)보다 앞선다
    {
        if (!emergency_active)    // 이미 비상 상태면 중복 처리 불필요
        {
            emergency_active = 1;
            Motor_Stop();         // 모터 즉시 정지 — 메인루프를 기다리지 않고 ISR에서 바로 끊는다
        }
        return;
    }

    /* 포토센서: current_floor 갱신 + 목표층 도달 시 Motor_Stop()만 수행.
       LED·부저·문 등 도착 후처리는 ISR에서 하지 않는다. 부저 엔벨로프처럼
       주기적 갱신이 필요한 동작은 메인루프의 Elevator_FSM_Update()에서 일괄 처리. */
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
