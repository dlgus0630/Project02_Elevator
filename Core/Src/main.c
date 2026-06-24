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
#include "iwdg.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "fnd.h"
#include "led.h"
#include "photo.h"
#include "stepper.h"
#include "button.h"
#include "servo.h"
#include "buzzer.h"
#include "dht.h"
#include "modbus.h"

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

/*
 * pending_action : 출발·도착 시퀀스 단계 번호
 *
 * [출발 시퀀스] — 버튼을 눌렀을 때
 * 0  IDLE        : 버튼 입력 허용
 * 1  DOOR_OPEN   : 문 열리는 중  (Servo_Door_Open 직후, DOOR_MOVE_MS 대기)
 * 2  BOARDING    : 탑승 대기 중  (문 완전히 열린 후,   BOARD_WAIT_MS 대기)
 * 3  DOOR_CLOSE  : 문 닫히는 중  (Servo_Door_Close 직후, DOOR_MOVE_MS 대기)
 * 4  DEPART      : 모터 출발     (Motor_Start → is_moving=1 → pending=0)
 *
 * [도착 시퀀스] — 포토센서가 목표층을 감지했을 때
 * 5  SETTLE      : 관성 안정화  (SETTLE_MS 대기)
 * 6  DOOR_OPEN2  : 문 열리는 중 (Servo_Door_Open 직후, DOOR_MOVE_MS 대기)
 * 7  ALIGHT      : 하차 대기 중 (문 완전히 열린 후,   BOARD_WAIT_MS 대기)
 * 8  DOOR_CLOSE2 : 문 닫히는 중 (Servo_Door_Close 직후, DOOR_MOVE_MS 대기)
 * → 0  IDLE
 */
uint8_t  pending_action = 0;   // ISR 접근 없음 → volatile 불필요
uint32_t pending_tick   = 0;   // 각 단계 시작 시각
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
  /* USER CODE BEGIN 2 */
  HAL_TIM_Base_Start(&htim5);
  DHT_Init();
  Modbus_Init();
  Servo_Init();
  FND_Shift_Data(current_floor);
  printf("=== Elevator Start ===\r\n");

  uint32_t last_alive_tick    = 0;
  uint8_t  is_moving          = 0;
  uint8_t  emergency_announced = 0;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* ==================================================================== */
    /* 0. 비상정지 처리 (최우선)                                             */
    /* 모터는 ISR에서 이미 정지됨. 이 블록만 반복, 아래 로직 완전 스킵   */
    /* ==================================================================== */
    if (emergency_active)
    {
        if (!emergency_announced)
        {
            emergency_announced = 1;
            is_moving           = 0;
            pending_action      = 0;
            Motor_Stop();           // ISR에서 이미 정지했지만 이중 확인
            LED_Bar_Arrive();       // led.c의 emergency_active 분기가 점멸 처리
            Buzzer_Emergency();     // "띵" 3회 연속 비상음
            printf("[EMERGENCY] Stop button pressed!\r\n");
        }
        LED_Bar_Update();
        Buzzer_Update();
        FND_Scan();
        DHT_Update();
        Modbus_Update();
        HAL_IWDG_Refresh(&hiwdg);
        continue;
    }

    /* ==================================================================== */
    /* 1. 버튼 스캔                                                          */
    /* 이동 중(is_moving=1)이거나 시퀀스 진행 중(pending≠0)이면          */
    /* 층 버튼·문 버튼 모두 무시                                           */
    /* ==================================================================== */
    ButtonEvent_t btn = Button_Scan();

    if (is_moving == 0 && pending_action == 0)
    {
        if      (btn == BTN_EVENT_FLOOR_1) target_floor = 1;
        else if (btn == BTN_EVENT_FLOOR_2) target_floor = 2;
        else if (btn == BTN_EVENT_FLOOR_3) target_floor = 3;
        else if (btn == BTN_EVENT_DOOR_OPEN)
        {
            /* 수동 문 열기 — 시퀀스 없이 즉시 열기만 */
            Buzzer_Ding();
            Servo_Door_Open();
            printf("[DOOR] Manual open\r\n");
        }
        else if (btn == BTN_EVENT_DOOR_CLOSE)
        {
            /* 수동 문 닫기 — 시퀀스 없이 즉시 닫기만 */
            Buzzer_Dong();
            Servo_Door_Close();
            printf("[DOOR] Manual close\r\n");
        }
    }

    /* ==================================================================== */
    /* 2. 출발 트리거                                                        */
    /* 층 버튼이 눌렸고, 현재 층과 다르고, IDLE 상태일 때 시퀀스 시작    */
    /* ==================================================================== */
    if (pending_action == 0 && is_moving == 0
        && target_floor != current_floor
        && (btn == BTN_EVENT_FLOOR_1
         || btn == BTN_EVENT_FLOOR_2
         || btn == BTN_EVENT_FLOOR_3))
    {
        /* step 1 : 문 열기 시작 */
        Buzzer_Ding();
        Servo_Door_Open();
        pending_action = 1;
        pending_tick   = HAL_GetTick();
        printf("[DEPART] Step1 Door opening. target=%d\r\n", target_floor);
    }

    /* ==================================================================== */
    /* 3. 출발 시퀀스 진행 (pending 1~4)                                    */
    /* ==================================================================== */

    /* step 1→2 : 문이 완전히 열리면 탑승 대기 시작 */
    if (pending_action == 1
        && HAL_GetTick() - pending_tick >= DOOR_MOVE_MS)
    {
        pending_action = 2;
        pending_tick   = HAL_GetTick();
        printf("[DEPART] Step2 Door open. Boarding wait %dms\r\n", BOARD_WAIT_MS);
    }

    /* step 2→3 : 탑승 대기가 끝나면 문 닫기 시작 */
    if (pending_action == 2
        && HAL_GetTick() - pending_tick >= BOARD_WAIT_MS)
    {
        Buzzer_Dong();
        Servo_Door_Close();
        pending_action = 3;
        pending_tick   = HAL_GetTick();
        printf("[DEPART] Step3 Door closing\r\n");
    }

    /* step 3→4 : 문이 완전히 닫히면 LED 출발 연출 후 모터 출발 */
    if (pending_action == 3
        && HAL_GetTick() - pending_tick >= DOOR_MOVE_MS)
    {
        LED_Bar_Depart();
        if (target_floor > current_floor)
        {
            Motor_Start(DIR_CW);
            LED_Bar_Go_Up();
        }
        else
        {
            Motor_Start(DIR_CCW);
            LED_Bar_Go_Down();
        }
        is_moving      = 1;
        pending_action = 0;   // 모터 주행 중에는 IDLE — 포토센서가 도착 감지
        printf("[DEPART] Step4 Motor started! target=%d\r\n", target_floor);
    }

    /* ==================================================================== */
    /* 4. 모터 논블로킹 구동                                                 */
    /* ==================================================================== */
    Update_Elevator_Motor();

    /* ==================================================================== */
    /* 5. 도착 감지 → 도착 시퀀스 시작 (pending 5~8)                        */
    /* 모터 정지는 photo.c ISR에서 수행됨                                 */
    /* 메인루프는 is_moving==1인데 current==target인 순간을 감지          */
    /* ==================================================================== */
    if (is_moving == 1 && current_floor == target_floor)
    {
        is_moving      = 0;
        FND_Shift_Data(current_floor);
        /* step 5 : 관성 안정화 대기 */
        pending_action = 5;
        pending_tick   = HAL_GetTick();
        printf("[ARRIVE] Step5 Settling. floor=%d\r\n", current_floor);
    }

    /* step 5→6 : 안정화 후 문 열기 시작 */
    if (pending_action == 5
        && HAL_GetTick() - pending_tick >= SETTLE_MS)
    {
        Buzzer_Ding();
        LED_Bar_Arrive();
        Servo_Door_Open();
        pending_action = 6;
        pending_tick   = HAL_GetTick();
        printf("[ARRIVE] Step6 Door opening\r\n");
    }

    /* step 6→7 : 문이 완전히 열리면 하차 대기 시작 */
    if (pending_action == 6
        && HAL_GetTick() - pending_tick >= DOOR_MOVE_MS)
    {
        pending_action = 7;
        pending_tick   = HAL_GetTick();
        printf("[ARRIVE] Step7 Door open. Alight wait %dms\r\n", BOARD_WAIT_MS);
    }

    /* step 7→8 : 하차 대기가 끝나면 문 닫기 시작 */
    if (pending_action == 7
        && HAL_GetTick() - pending_tick >= BOARD_WAIT_MS)
    {
        Buzzer_Dong();
        Servo_Door_Close();
        pending_action = 8;
        pending_tick   = HAL_GetTick();
        printf("[ARRIVE] Step8 Door closing\r\n");
    }

    /* step 8→0 : 문이 완전히 닫히면 IDLE — 다음 입력 허용 */
    if (pending_action == 8
        && HAL_GetTick() - pending_tick >= DOOR_MOVE_MS)
    {
        pending_action = 0;
        printf("[ARRIVE] Sequence done. Ready.\r\n");
    }

    /* ==================================================================== */
    /* 6. LED 바 논블로킹 애니메이션                                         */
    /* ==================================================================== */
    LED_Bar_Update();

    /* ==================================================================== */
    /* 7. 부저 논블로킹 구동                                                 */
    /* ==================================================================== */
    Buzzer_Update();

    /* ==================================================================== */
    /* 8. FND 갱신                                                           */
    /* ==================================================================== */
    FND_Scan();
    DHT_Update();                      // ← 추가 : 메인루프 작동 중 주기적인 DHT 측정 실행

    /* ==================================================================== */
    /* 9. 1초 생존 신호                                                      */
    /* ==================================================================== */
    if (HAL_GetTick() - last_alive_tick >= 1000)
    {
        printf("Alive floor=%d target=%d moving=%d pending=%d\r\n",
               current_floor, target_floor, is_moving, pending_action);
        last_alive_tick = HAL_GetTick();
    }

    /* ==================================================================== */
    /* 10. 워치독 리프레시                                                   */
    /* ==================================================================== */
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
