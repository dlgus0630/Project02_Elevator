#include "dev_servo.h"
#include <stdio.h>

/* Servo_Init — PWM 출력 시작 + 초기 상태를 '문 닫힘(0도)'으로 설정 */
void Servo_Init(void)
{
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    htim3.Instance->CCR1 = SERVO_CCR_0DEG;

    printf("[SERVO] Init Success. Door CLOSED (0 deg, CCR1=%d)\r\n", SERVO_CCR_0DEG);
}

/* Servo_Door_Open — 180도로 회전, 문 완전 개방
 * CCR1만 바꾸고 즉시 반환한다(논블로킹). 서보가 실제로 다 움직이는 데는
 * 수백 ms가 걸리므로, 문 열림 유지 시간은 상위 FSM에서 따로 관리한다. */
void Servo_Door_Open(void)
{
    htim3.Instance->CCR1 = SERVO_CCR_180DEG;
    printf("[SERVO] Door OPEN  (180 deg, CCR1=%d)\r\n", SERVO_CCR_180DEG);
}

/* Servo_Door_Close — 0도로 회전, 문 완전 폐쇄 (Open과 동일하게 논블로킹) */
void Servo_Door_Close(void)
{
    htim3.Instance->CCR1 = SERVO_CCR_0DEG;
    printf("[SERVO] Door CLOSE (0 deg, CCR1=%d)\r\n", SERVO_CCR_0DEG);
}
