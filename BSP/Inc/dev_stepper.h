#ifndef INC_STEPPER_H_
#define INC_STEPPER_H_

#include "main.h"

/* ── 속도 설정 ── */
#define STEP_INTERVAL_MS    5       // 28BYJ-48: 5~10ms 권장

/* ── 방향 ── */
#define DIR_CW   0   // 상승 (1→2→3층)
#define DIR_CCW  1   // 하강 (3→2→1층)

/* ── 스테퍼 핀 (PC5, PC6, PC8, PA12) ── */
#define EV_PIN_1_PORT   GPIOC
#define EV_PIN_1_PIN    GPIO_PIN_5

#define EV_PIN_2_PORT   GPIOC
#define EV_PIN_2_PIN    GPIO_PIN_6

#define EV_PIN_3_PORT   GPIOC
#define EV_PIN_3_PIN    GPIO_PIN_8

#define EV_PIN_4_PORT   GPIOA
#define EV_PIN_4_PIN    GPIO_PIN_12

/* ── 함수 선언 ── */
void Motor_Start(uint8_t direction);   // 모터 켜기 (방향 지정)
void Motor_Stop(void);                 // 모터 끄기
void Update_Elevator_Motor(void);      // 메인루프에서 매번 호출

#endif
