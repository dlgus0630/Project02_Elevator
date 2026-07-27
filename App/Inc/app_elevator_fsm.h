#ifndef INC_APP_ELEVATOR_FSM_H_
#define INC_APP_ELEVATOR_FSM_H_

#include "main.h"

/* enum 값이 dev_display.h의 Display_State_t와 일치해야 함 (캐스팅으로 전달) */
typedef enum {
    ELEV_STATE_IDLE = 0,
    ELEV_STATE_MOVING,
    ELEV_STATE_DOOR_OPEN,
    ELEV_STATE_ERROR,
    ELEV_STATE_INSPECTION
} ElevatorState_t;

void            Elevator_FSM_Init(void);
void            Elevator_FSM_Update(void);
ElevatorState_t Elevator_FSM_GetState(void);
uint16_t        Elevator_FSM_GetErrorCode(void);

#endif /* INC_APP_ELEVATOR_FSM_H_ */
