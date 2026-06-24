#ifndef INC_BUZZER_H_
#define INC_BUZZER_H_

#include "main.h"

/* TIM2 핸들 (tim.c에서 선언됨) */
extern TIM_HandleTypeDef htim2;

/* 음계별 ARR 설정값 (TIM2 PSC=99, 1MHz 기준) */
#define NOTE_B4    2024   // 494 Hz
#define NOTE_D5    1702   // 587 Hz
#define NOTE_E5    1516   // 659 Hz
#define NOTE_A5    1135   // 880 Hz (비상음용)

/* 부저 제어 함수 */
void Buzzer_Ding(void);       // "띵~" (도착/문 열림)
void Buzzer_Dong(void);       // "동~" (문 닫힘)
void Buzzer_Emergency(void);  // "띵띵띵" (비상 경보)
void Buzzer_Update(void);     // 논블로킹 업데이트 (메인 루프 호출)

#endif /* INC_BUZZER_H_ */
