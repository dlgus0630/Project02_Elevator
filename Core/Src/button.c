#include "button.h"


/* 버튼 정보 구조체
 * - port      : GPIO 포트
 * - pin       : GPIO 핀 번호
 * - last_tick : 마지막 입력 시간 (디바운스용) */
typedef struct
{
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint32_t      last_tick;

} Button_t;


/* 버튼 목록
 * 인덱스 순서 = ButtonEvent_t 값 (1~5) */
static Button_t buttons[] =
{
    { GPIOC, GPIO_PIN_4,  0 },   // [0] → BTN_EVENT_FLOOR_1
    { GPIOB, GPIO_PIN_1,  0 },   // [1] → BTN_EVENT_FLOOR_2
    { GPIOB, GPIO_PIN_13, 0 },   // [2] → BTN_EVENT_FLOOR_3
    { GPIOB, GPIO_PIN_14, 0 },   // [3] → BTN_EVENT_DOOR_OPEN
    { GPIOB, GPIO_PIN_15, 0 }    // [4] → BTN_EVENT_DOOR_CLOSE
};


/*────────────────────────────────────────────
 * Button_Scan
 *
 * 메인 루프에서 반복 호출
 * 버튼 입력 감지 → 이벤트 반환
 * 입력 없음 → BTN_EVENT_NONE
 *────────────────────────────────────────────*/
ButtonEvent_t Button_Scan(void)
{
    uint32_t now = HAL_GetTick();

    for (int i = 0; i < 5; i++)
    {
        /* 버튼 눌림 확인 */
        if (HAL_GPIO_ReadPin(buttons[i].port,
                             buttons[i].pin) == BUTTON_ACTIVE_STATE)
        {
            /* 디바운스 시간 통과 확인 */
            if (now - buttons[i].last_tick > BUTTON_DEBOUNCE_MS)
            {
                buttons[i].last_tick = now;

                return (ButtonEvent_t)(i + 1);
            }
        }
    }

    return BTN_EVENT_NONE;
}
