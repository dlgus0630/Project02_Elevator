#ifndef INC_APP_ELEVATOR_FSM_H_
#define INC_APP_ELEVATOR_FSM_H_

#include "main.h"

/* ======================================================
 * 엘리베이터 상태 (FSM 상태값)
 *
 * IDLE       : 정지 상태. 버튼 입력을 받아 목표층을 정한다
 * MOVING     : 모터 구동 중. 포토센서가 목표층을 감지하면 종료
 * DOOR_OPEN  : 문 동작 구간. 내부적으로 4단계(SETTLE/OPENING/DWELL/CLOSING)로
 *              나뉘며, 출발 전 승객 탑승과 도착 후 하차를 모두 이 상태에서 처리
 * ERROR      : 비상정지·층 건너뜀·이동 타임아웃으로 잠긴 상태. 원인이 사라져야 해제
 * INSPECTION : 관제실에서 내린 점검 모드. 원인이 사라져야 해제
 *
 * ※ 이 enum의 값 순서는 dev_display.h의 Display_State_t와 반드시 같아야 한다.
 *   FSM 상태를 그대로 캐스팅해서 Display_Update()에 넘기기 때문
 * ====================================================== */
typedef enum {
    ELEV_STATE_IDLE = 0,
    ELEV_STATE_MOVING,
    ELEV_STATE_DOOR_OPEN,
    ELEV_STATE_ERROR,
    ELEV_STATE_INSPECTION
} ElevatorState_t;

void            Elevator_FSM_Init(void);     // 부팅 시 1회 호출 (현재 층 확인 포함)
void            Elevator_FSM_Update(void);   // 메인루프에서 매번 호출 (논블로킹)
ElevatorState_t Elevator_FSM_GetState(void);
uint16_t        Elevator_FSM_GetErrorCode(void);   // 0이면 정상, 그 외는 오류코드

#endif /* INC_APP_ELEVATOR_FSM_H_ */
