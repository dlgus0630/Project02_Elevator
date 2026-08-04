#ifndef INC_STEPPER_H_
#define INC_STEPPER_H_

#include "main.h"

/* ── 속도 설정 ──
 * 한 스텝 사이의 간격(ms). 값이 작을수록 빠르지만, 너무 줄이면 로터가
 * 자기장을 못 따라가 탈조(헛도는 현상)가 난다. 28BYJ-48은 5~10ms 권장. */
#define STEP_INTERVAL_MS    5

/* ── 방향 ── */
#define DIR_CW   0   // 상승 (1→2→3층)
#define DIR_CCW  1   // 하강 (3→2→1층)

/* ── 28BYJ-48 스테퍼 핀 (ULN2003 드라이버 IN1~IN4에 연결) ── */
#define EV_PIN_1_PORT   GPIOC        // IN1 = PC5
#define EV_PIN_1_PIN    GPIO_PIN_5

#define EV_PIN_2_PORT   GPIOC        // IN2 = PC6
#define EV_PIN_2_PIN    GPIO_PIN_6

#define EV_PIN_3_PORT   GPIOC        // IN3 = PC8
#define EV_PIN_3_PIN    GPIO_PIN_8

#define EV_PIN_4_PORT   GPIOA        // IN4 = PA12
#define EV_PIN_4_PIN    GPIO_PIN_12

/* ── 함수 선언 ── */
void Motor_Start(uint8_t direction);   // 모터 켜기 (DIR_CW / DIR_CCW)
void Motor_Stop(void);                 // 모터 끄기 (네 핀 모두 LOW → 코일 전류 차단)
void Update_Elevator_Motor(void);      // 메인루프에서 매번 호출

#endif
