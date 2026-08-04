#include "dev_photo.h"
#include "dev_stepper.h"
#include <stdio.h>

/* target_floor는 main.c에서 정의 — 도달 여부 비교에만 사용 */
extern volatile uint8_t target_floor;

void Handle_Photo_Interrupt(uint16_t GPIO_Pin)
{
    /* ── 디바운싱 300ms ──
     * 카가 센서 슬릿을 통과할 때 광 경계에서 엣지가 여러 번 튀어 같은 층이
     * 중복 감지되는 것을 막는다. 3개 센서가 공유하는 static 변수라, 인접 층을
     * 300ms 안에 연달아 지날 만큼 빠른 이동은 없다는 전제에 기댄다. */
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
    if (detected == 2) floor2_transit_flag = 1;   // 2층 센서를 지나쳤음을 기록 (E301 판정용)
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

/* 부팅 시 위치 확인용. 인터럽트는 엣지(변화 순간)에만 걸리므로 전원을 켤 때
 * 이미 어느 층에 서 있으면 감지되지 않는다. 그래서 3개 센서 핀을 직접 레벨로
 * 읽는다. 풀업+FALLING 설정이라 LOW(RESET)면 그 층 센서 위에 있다는 뜻. */
uint8_t Photo_DetectCurrentFloor(void)
{
    if (HAL_GPIO_ReadPin(Photo_EXTIA11_GPIO_Port, Photo_EXTIA11_Pin) == GPIO_PIN_RESET) return 1;
    if (HAL_GPIO_ReadPin(Photo_EXTIB12_GPIO_Port, Photo_EXTIB12_Pin) == GPIO_PIN_RESET) return 2;
    if (HAL_GPIO_ReadPin(Photo_EXTI_GPIO_Port,    Photo_EXTI_Pin)    == GPIO_PIN_RESET) return 3;
    return 0;   /* 층 사이(어느 센서도 감지 안 됨) */
}
