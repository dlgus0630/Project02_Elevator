#include "dev_button.h"


/* 버튼 1개의 정보
 * last_tick : 이 버튼이 마지막으로 인정된 시각(ms). 버튼마다 따로 두어야
 *             한 버튼의 디바운스가 다른 버튼 입력을 막지 않는다. */
typedef struct
{
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint32_t      last_tick;

} Button_t;


/* 버튼 목록
 * 배열 순서가 곧 이벤트 값이다(인덱스 i → ButtonEvent_t 값 i+1).
 * 순서를 바꾸면 반환 이벤트가 어긋나므로 주의. */
static Button_t buttons[] =
{
    { GPIOB, GPIO_PIN_1,  0 },   // [0] → BTN_EVENT_FLOOR_1
    { GPIOB, GPIO_PIN_15, 0 },   // [1] → BTN_EVENT_FLOOR_2
    { GPIOB, GPIO_PIN_14, 0 },   // [2] → BTN_EVENT_FLOOR_3
    { GPIOB, GPIO_PIN_13, 0 },   // [3] → BTN_EVENT_DOOR_OPEN
    { GPIOC, GPIO_PIN_4,  0 }    // [4] → BTN_EVENT_DOOR_CLOSE
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
        if (HAL_GPIO_ReadPin(buttons[i].port,
                             buttons[i].pin) == BUTTON_ACTIVE_STATE)
        {
            /* 마지막 인정 시각으로부터 BUTTON_DEBOUNCE_MS가 지나야 새 입력으로
             * 인정한다. 기계식 접점의 채터링(짧게 여러 번 붙었다 떨어지는 현상)과
             * 손가락을 계속 누르고 있을 때의 연타 입력을 함께 걸러낸다. */
            if (now - buttons[i].last_tick > BUTTON_DEBOUNCE_MS)
            {
                buttons[i].last_tick = now;

                return (ButtonEvent_t)(i + 1);
            }
        }
    }

    return BTN_EVENT_NONE;
}
