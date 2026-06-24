#include "photo.h"
#include "stepper.h"
#include <stdio.h>

/* target_floor는 main.c에서 정의 — 도달 여부 비교에만 사용 */
extern volatile uint8_t target_floor;

void Handle_Photo_Interrupt(uint16_t GPIO_Pin)
{
    /* ── 디바운싱 300ms ── */
    static uint32_t last_time = 0;
    uint32_t now = HAL_GetTick();
    if (now - last_time < 300) return;
    last_time = now;

    /* ── 어느 층인지 판단 ── */
    uint8_t detected = 0;
    if      (GPIO_Pin == GPIO_PIN_11) detected = 1;  // PA11 → 1층
    else if (GPIO_Pin == GPIO_PIN_12) detected = 2;  // PB12 → 2층
    else if (GPIO_Pin == GPIO_PIN_2)  detected = 3;  // PB2  → 3층
    else return;

    /* ── 현재 층 갱신 ── */
    current_floor = detected;
    printf("[PHOTO] floor=%d target=%d\r\n", current_floor, target_floor);

    /* ── 목표층 도달 → 모터만 즉시 정지 ──
     * LED·부저·FND·문 열기 대기는 메인루프 step 4에서 단일 처리.
     * ISR에서 Buzzer_Update() 없이 Buzzer_Ding()을 부르면
     * 엔벨로프가 동작하지 않으므로 여기서는 Motor_Stop()만 수행한다. */
    if (current_floor == target_floor)
    {
        Motor_Stop();
        printf("[PHOTO] Motor stopped at floor %d\r\n", current_floor);
    }
}
