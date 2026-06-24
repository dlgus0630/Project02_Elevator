#ifndef FND_H
#define FND_H

#include "main.h"

/* 외부 전역 변수 선언 (main.c에 있는 변수를 가져옴) */
extern volatile uint8_t current_floor;
extern volatile uint8_t emergency_active;

/* 함수 선언 */
void FND_Shift_Data(uint8_t data);
void FND_Scan(void);

#endif
