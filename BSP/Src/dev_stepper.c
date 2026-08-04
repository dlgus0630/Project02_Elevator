#include "dev_stepper.h"
#include <stdio.h>

/* ── 하프스텝 시퀀스 (8단계) ──
 * 각 행은 IN1~IN4에 줄 값이며, 한 상(相) → 두 상 동시 여자를 번갈아 반복한다.
 * 이 순서 자체가 회전 방향을 만들므로 행 순서를 바꾸면 안 된다.
 *
 * (한때 풀스텝 4단계로 바꿔 속도 2배를 노렸으나, 한 스텝당 회전각이
 *  커지면서 이 부하/속도에서 로터가 못 따라가 탈조 발생 → 오히려
 *  더 느려짐. 하프스텝이 더 안정적이라 되돌림. 속도는 STEP_INTERVAL_MS로
 *  조심스럽게 조정할 것.) */
static const uint8_t HALF_STEP[8][4] = {
    {1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,1,1,0},
    {0,0,1,0}, {0,0,1,1}, {0,0,0,1}, {1,0,0,1}
};

/* ── 내부 상태 ── */
static uint8_t  s_running    = 0;       // 1이면 모터 구동 중
static uint8_t  s_direction  = DIR_CW;
static uint8_t  s_step_index = 0;
static uint32_t s_last_tick  = 0;

/* ── 시퀀스 한 행을 네 핀에 그대로 출력 ── */
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

/* ── 모터 끄기 ──
 * 네 핀을 모두 LOW로 내려 코일 전류를 끊는다(계속 켜두면 발열/전류 낭비).
 * 대신 유지 토크가 사라지므로 정지 위치가 약간 밀릴 수 있다. */
void Motor_Stop(void)
{
    s_running = 0;
    HAL_GPIO_WritePin(EV_PIN_1_PORT, EV_PIN_1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EV_PIN_2_PORT, EV_PIN_2_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EV_PIN_3_PORT, EV_PIN_3_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EV_PIN_4_PORT, EV_PIN_4_PIN, GPIO_PIN_RESET);
    printf("[MOTOR] Stop\r\n");
}

/* ── 메인루프에서 매번 호출 (논블로킹) ──
 * HAL_GetTick()으로 STEP_INTERVAL_MS가 지났을 때만 한 스텝 진행한다.
 * 딜레이로 붙잡지 않아야 문·통신·워치독 처리가 같이 돌아간다. */
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
