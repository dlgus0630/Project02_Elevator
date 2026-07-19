#ifndef INC_BUTTON_H_
#define INC_BUTTON_H_

#include "main.h"

/* 버튼이 눌렸을 때의 신호 레벨 (Active-Low: 풀업 회로) */
#define BUTTON_ACTIVE_STATE  GPIO_PIN_RESET

/* 디바운스 시간 (ms): 이 시간 안에 연속 입력은 1회로 처리 */
#define BUTTON_DEBOUNCE_MS   50

/* 버튼 이벤트 열거형 — Button_Scan() 반환값 */
typedef enum {
    BTN_EVENT_NONE       = 0,
    BTN_EVENT_FLOOR_1    = 1,
    BTN_EVENT_FLOOR_2    = 2,
    BTN_EVENT_FLOOR_3    = 3,
    BTN_EVENT_DOOR_OPEN  = 4,
    BTN_EVENT_DOOR_CLOSE = 5,
} ButtonEvent_t;

/* 메인루프에서 매번 호출 — 눌린 버튼 이벤트 반환, 없으면 NONE */
ButtonEvent_t Button_Scan(void);

#endif /* INC_BUTTON_H_ */
