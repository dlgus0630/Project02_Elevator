#ifndef INC_SERVO_H_
#define INC_SERVO_H_

#include "main.h"

/* TIM3 CH1 = PA6, SG90 서보 PWM 출력 */
extern TIM_HandleTypeDef htim3;

/* SG90은 20ms 주기(50Hz) 펄스의 HIGH 폭으로 각도가 정해진다.
 * TIM3: PSC=2000-1, ARR=1000-1 @APB1 타이머 100MHz → 50Hz, CCR 1칸 = 0.02ms
 *   0도:   CCR=25   (펄스 폭 0.5ms)
 *   180도: CCR=125  (펄스 폭 2.5ms)
 * 기구부/서보 개체차에 따라 0도(25~50), 180도(100~125) 범위에서 미세조정 가능
 */
#define SERVO_CCR_0DEG      25
#define SERVO_CCR_180DEG   125

void Servo_Init(void);          // PWM 출력 시작, 초기 상태는 문 닫힘(0도)
void Servo_Door_Open(void);     // 문 열기(180도)
void Servo_Door_Close(void);    // 문 닫기(0도)

#endif
