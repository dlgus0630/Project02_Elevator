#include "stepper.h"
#include "main.h"
#include <stdio.h>

/* ── 하프스텝 시퀀스 (8단계) ── */
static const uint8_t HALF_STEP[8][4] = {
    {1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,1,1,0},
    {0,0,1,0}, {0,0,1,1}, {0,0,0,1}, {1,0,0,1}
};

/* ── 내부 상태 ── */
static uint8_t  s_running    = 0;       // 1이면 모터 구동 중
static uint8_t  s_direction  = DIR_CW;
static uint8_t  s_step_index = 0;
static uint32_t s_last_tick  = 0;

/* ── 핀 출력 ── */
static void step_output(uint8_t idx)
{
    HAL_GPIO_WritePin(EV_PIN_1_PORT, EV_PIN_1_PIN, HALF_STEP[idx][0] ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EV_PIN_2_PORT, EV_PIN_2_PIN, HALF_STEP[idx][1] ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EV_PIN_3_PORT, EV_PIN_3_PIN, HALF_STEP[idx][2] ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EV_PIN_4_PORT, EV_PIN_4_PIN, HALF_STEP[idx][3] ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ── 모터 켜기 ── */
void Motor_Start(uint8_t direction)
{
    s_direction  = direction;
    s_running    = 1;
    s_last_tick  = HAL_GetTick();
    printf("[MOTOR] Start dir=%s\r\n", direction == DIR_CW ? "UP" : "DOWN");
}

/* ── 모터 끄기 (코일 전류 차단) ── */
void Motor_Stop(void)
{
    s_running = 0;
    HAL_GPIO_WritePin(EV_PIN_1_PORT, EV_PIN_1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EV_PIN_2_PORT, EV_PIN_2_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EV_PIN_3_PORT, EV_PIN_3_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EV_PIN_4_PORT, EV_PIN_4_PIN, GPIO_PIN_RESET);
    printf("[MOTOR] Stop\r\n");
}

/* ── 메인루프에서 매번 호출 (논블로킹) ── */
void Update_Elevator_Motor(void)
{
    if (!s_running) return;

    uint32_t now = HAL_GetTick();
    if (now - s_last_tick < STEP_INTERVAL_MS) return;
    s_last_tick = now;

    // 방향에 따라 시퀀스 인덱스 증가/감소
    if (s_direction == DIR_CW)
        s_step_index = (s_step_index + 1) % 8;
    else
        s_step_index = (s_step_index == 0) ? 7 : s_step_index - 1;

    step_output(s_step_index);
}
