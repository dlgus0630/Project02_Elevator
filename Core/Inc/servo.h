#ifndef INC_SERVO_H_
#define INC_SERVO_H_

#include "main.h"

extern TIM_HandleTypeDef htim3;

/* 50Hz(PSC=2000-1, ARR=1000-1) 기준 CCR1 매핑값
 * 0도(보수적):   CCR=50   (1.0ms)
 * 90도(중간):    CCR=75   (1.5ms)
 * 180도(보수적): CCR=100  (2.0ms)
 * 기구부/서보 특성에 따라 0도(25~50), 180도(100~125) 범위에서 미세조정 가능
 */
#define SERVO_CCR_0DEG      50
#define SERVO_CCR_90DEG     75
#define SERVO_CCR_180DEG   100

void Servo_Init(void);          // PWM 시작 및 초기화(0도)
void Servo_Door_Open(void);     // 문 열기(180도)
void Servo_Door_Close(void);    // 문 닫기(0도)

#endif
