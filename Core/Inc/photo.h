#ifndef __PHOTO_H__
#define __PHOTO_H__

#include "main.h"

/* 현재 층 전역 변수 (main.c에서 정의, 포토 ISR이 갱신) */
extern volatile uint8_t current_floor;

/* 포토센서 인터럽트 처리 함수
 * 역할: 층 감지 후 current_floor 갱신 + 목표층 도달 시 Motor_Stop()만 수행.
 * LED·부저 등 도착 후속 처리는 메인루프 step 4에서 단일 처리한다. */
void Handle_Photo_Interrupt(uint16_t GPIO_Pin);

#endif /* __PHOTO_H__ */
