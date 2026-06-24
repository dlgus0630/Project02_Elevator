#include "led.h"

/* 애니메이션 상태 머신 */
typedef enum {
    LED_ANIM_IDLE = 0,     // 대기(전체 꺼짐 또는 연출 없음)
    LED_ANIM_DEPART_BLINK, // 출발 전 깜빡임
    LED_ANIM_UP,           // 상승 주행
    LED_ANIM_DOWN,         // 하강 주행
    LED_ANIM_ARRIVE_BLINK  // 도착 후 깜빡임
} LED_Anim_t;

static LED_Anim_t s_anim      = LED_ANIM_IDLE;
static uint8_t    s_step      = 0;   // 주행 애니메이션 현재 LED 위치(0~7)
static uint32_t   s_last_tick = 0;   // 마지막 패턴 갱신 시각(ms)
static uint8_t    s_blink_cnt = 0;   // 깜빡임 횟수 카운터

#define RUN_INTERVAL_MS    250  // 주행 애니메이션 속도
#define BLINK_INTERVAL_MS  250  // 깜빡임 속도

/* shift_out — 74HC595에 1바이트(8비트) 시프트 출력, MSB부터 송신 */
static void shift_out(uint8_t data)
{
    HAL_GPIO_WritePin(LED_LATCH_PORT, LED_LATCH_PIN, GPIO_PIN_RESET); // 래치 LOW

    for (int i = 7; i >= 0; i--)
    {
        HAL_GPIO_WritePin(LED_CLK_PORT, LED_CLK_PIN, GPIO_PIN_RESET);

        if (data & (1 << i))
            HAL_GPIO_WritePin(LED_DATA_PORT, LED_DATA_PIN, GPIO_PIN_SET);
        else
            HAL_GPIO_WritePin(LED_DATA_PORT, LED_DATA_PIN, GPIO_PIN_RESET);

        HAL_GPIO_WritePin(LED_CLK_PORT, LED_CLK_PIN, GPIO_PIN_SET);
    }

    HAL_GPIO_WritePin(LED_LATCH_PORT, LED_LATCH_PIN, GPIO_PIN_SET); // 래치 HIGH → 출력 반영
}

static void led_all_on(void)  { shift_out(0xFF); }
static void led_all_off(void) { shift_out(0x00); }

/* LED_Bar_Depart — 출발 깜빡임 시작 */
void LED_Bar_Depart(void)
{
    s_anim = LED_ANIM_DEPART_BLINK;
    s_blink_cnt = 0;
    s_last_tick = HAL_GetTick();
    led_all_on();
}

/* LED_Bar_Arrive — 도착 깜빡임 시작 */
void LED_Bar_Arrive(void)
{
    s_anim = LED_ANIM_ARRIVE_BLINK;
    s_blink_cnt = 0;
    s_last_tick = HAL_GetTick();
    led_all_on();
}

/* LED_Bar_Go_Up — 상승 주행 애니메이션 시작(아래→위) */
void LED_Bar_Go_Up(void)
{
    s_anim      = LED_ANIM_UP;
    s_step      = 0;
    s_last_tick = HAL_GetTick();
}

/* LED_Bar_Go_Down — 하강 주행 애니메이션 시작(위→아래) */
void LED_Bar_Go_Down(void)
{
    s_anim      = LED_ANIM_DOWN;
    s_step      = 7;
    s_last_tick = HAL_GetTick();
}

/* LED_Bar_Update — 메인루프에서 매번 호출(HAL_Delay 없는 논블로킹 코어) */
void LED_Bar_Update(void)
{
    uint32_t now = HAL_GetTick();

    /* 비상 상태 우선 처리: 0.5초 주기로 전체 ON/OFF 반복 */
    if (emergency_active) {
        if ((now / 500) % 2 == 0) shift_out(0xFF);
        else                      shift_out(0x00);
        return;
    }

    switch (s_anim)
    {
        /* 출발 깜빡임: 총 3회(On-Off 3쌍) 후 IDLE */
        case LED_ANIM_DEPART_BLINK:
            if (now - s_last_tick >= BLINK_INTERVAL_MS)
            {
                s_last_tick = now;
                s_blink_cnt++;

                if (s_blink_cnt < 6) {
                    if (s_blink_cnt % 2 == 1) led_all_off();
                    else led_all_on();
                } else {
                    led_all_on();           // 주행 준비를 위해 전체 ON 유지
                    s_anim = LED_ANIM_IDLE;
                }
            }
            break;

        /* 도착 깜빡임: 총 3회 후 전체 OFF */
        case LED_ANIM_ARRIVE_BLINK:
            if (now - s_last_tick >= BLINK_INTERVAL_MS)
            {
                s_last_tick = now;
                s_blink_cnt++;

                if (s_blink_cnt < 6) {
                    if (s_blink_cnt % 2 == 1) led_all_off();
                    else led_all_on();
                } else {
                    led_all_off();
                    s_anim = LED_ANIM_IDLE;
                }
            }
            break;

        /* 상승 주행: 3칸이 잔상(꼬리)을 남기며 아래→위로 흐름 */
        case LED_ANIM_UP:
            if (now - s_last_tick >= RUN_INTERVAL_MS)
            {
                s_last_tick = now;

                uint8_t step1 = s_step;
                uint8_t step2 = (s_step == 0) ? 7 : s_step - 1;
                uint8_t step3 = (step2 == 0) ? 7 : step2 - 1;

                shift_out((1 << step1) | (1 << step2) | (1 << step3));
                s_step = (s_step + 1) % 8;
            }
            break;

        /* 하강 주행: 3칸이 잔상(꼬리)을 남기며 위→아래로 흐름 */
        case LED_ANIM_DOWN:
            if (now - s_last_tick >= RUN_INTERVAL_MS)
            {
                s_last_tick = now;

                uint8_t step1 = s_step;
                uint8_t step2 = (s_step == 7) ? 0 : s_step + 1;
                uint8_t step3 = (step2 == 7) ? 0 : step2 + 1;

                shift_out((1 << step1) | (1 << step2) | (1 << step3));
                s_step = (s_step == 0) ? 7 : s_step - 1;
            }
            break;

        default:
            break;
    }
}
