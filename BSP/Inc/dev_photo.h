#ifndef __PHOTO_H__
#define __PHOTO_H__

#include "main.h"

/* 현재 층 전역 변수 (main.c에서 정의, 포토 ISR이 갱신) */
extern volatile uint8_t current_floor;

/* 포토센서 인터럽트 처리 함수
 * 역할: 층 감지 후 current_floor 갱신 + 목표층 도달 시 Motor_Stop()만 수행.
 * LED·부저 등 도착 후속 처리는 메인루프 step 4에서 단일 처리한다. */
void Handle_Photo_Interrupt(uint16_t GPIO_Pin);

/* 부팅 시 3개 포토센서를 직접 레벨로 읽어 "지금 실제로 어느 층에 있는지"
 * 확인한다. 인터럽트는 엣지(변화 순간)에만 걸리므로, 전원을 켤 때 이미
 * 2층/3층에 정지해 있으면 감지가 안 되어 current_floor(기본값 1)와
 * 실제 위치가 어긋나는 문제를 막기 위함.
 * 반환값: 감지된 층(1~3), 어느 센서도 감지 안 되면(층 사이) 0. */
uint8_t Photo_DetectCurrentFloor(void);

#endif /* __PHOTO_H__ */
