#ifndef INC_APP_ELEVATOR_FSM_H_
#define INC_APP_ELEVATOR_FSM_H_

#include "main.h"

/* =========================================================
 * 엘리베이터 카 제어 FSM (3계층 — Application Layer)
 *
 * 5-state 설계: IDLE / MOVING / DOOR_OPEN / ERROR / INSPECTION
 *   - 문 개폐 타이밍(OPENING→DWELL→CLOSING)과 도착 안정화(SETTLE)는
 *     DOOR_OPEN 상태 내부의 사설 페이즈로 완전히 숨긴다(헤더 비노출).
 *   - INSPECTION(점검)은 관리실 원격 명령으로 진입/해제하는 상태로,
 *     ERROR(비상)와 유사하게 카를 잠그되 물리적 비상정지보다는 우선순위가 낮다.
 *   - enum 값은 dev_display.h의 Display_State_t와 일치시켜
 *     Display_Update()에 캐스팅만으로 넘길 수 있게 한다.
 * ========================================================= */
typedef enum {
    ELEV_STATE_IDLE = 0,
    ELEV_STATE_MOVING,
    ELEV_STATE_DOOR_OPEN,
    ELEV_STATE_ERROR,
    ELEV_STATE_INSPECTION
} ElevatorState_t;

void            Elevator_FSM_Init(void);     // FSM 상태 초기화 + Display_Init 호출
void            Elevator_FSM_Update(void);   // 메인루프에서 매 tick 호출
ElevatorState_t Elevator_FSM_GetState(void); // modbus 등 외부 조회용 접근자

#endif /* INC_APP_ELEVATOR_FSM_H_ */
