#include "app_elevator_fsm.h"

#include <stdio.h>
#include "dev_display.h"
#include "dev_button.h"
#include "dev_servo.h"
#include "dev_stepper.h"
#include "dev_buzzer.h"
#include "dev_photo.h"
#include "dev_dht.h"

extern volatile uint8_t move_timeout_active;   /* E201 */
extern volatile uint8_t floor_skip_active;     /* E301 */
extern volatile uint8_t floor2_transit_flag;

#define DOOR_MOVE_MS   2000
#define BOARD_WAIT_MS  3000
#define SETTLE_MS      3000

/* DOOR_OPEN 내부 사설 페이즈 (헤더 비노출) */
typedef enum {
    DOOR_PHASE_SETTLE = 0,
    DOOR_PHASE_OPENING,
    DOOR_PHASE_DWELL,
    DOOR_PHASE_CLOSING
} DoorPhase_t;

static ElevatorState_t s_state      = ELEV_STATE_IDLE;
static DoorPhase_t     s_door_phase = DOOR_PHASE_SETTLE;
static uint32_t        s_phase_tick = 0;

#define MOVE_TIMEOUT_1F_MS  60000    /* 실측 확정값 */
#define MOVE_TIMEOUT_2F_MS  120000   /* 실측 확정값 */
#define TEMP_ALARM_ON_C   31.0f
#define TEMP_ALARM_OFF_C  28.0f

/* 1<->3층처럼 중간에 2층을 반드시 거치는 이동인지 판정 (E201 타임아웃 기준값, E301 판정 공용) */
static inline uint8_t Is_TwoFloorTrip(uint8_t from, uint8_t to)
{
    return (from == 1 && to == 3) || (from == 3 && to == 1);
}

static uint32_t s_move_start_tick    = 0;
static uint8_t  s_depart_floor       = 1;
static uint32_t s_move_timeout_ms    = MOVE_TIMEOUT_1F_MS;
static uint8_t  s_temp_alarm_latched = 0;   /* E101 히스테리시스 래치 */
static uint16_t s_error_code         = 0;

static void Door_EnterPhase(DoorPhase_t phase)
{
    s_door_phase = phase;
    s_phase_tick = HAL_GetTick();

    switch (phase)
    {
        case DOOR_PHASE_OPENING:
            Buzzer_Ding();
            Servo_Door_Open();
            break;

        case DOOR_PHASE_CLOSING:
            Buzzer_Dong();
            Servo_Door_Close();
            break;

        case DOOR_PHASE_SETTLE:
        case DOOR_PHASE_DWELL:
        default:
            break;
    }
}

static void FSM_HandleIdle(void)
{
    ButtonEvent_t btn = Button_Scan();

    if      (btn == BTN_EVENT_FLOOR_1) target_floor = 1;
    else if (btn == BTN_EVENT_FLOOR_2) target_floor = 2;
    else if (btn == BTN_EVENT_FLOOR_3) target_floor = 3;
    else if (btn == BTN_EVENT_DOOR_OPEN)
    {
        Buzzer_Ding();
        Servo_Door_Open();
        printf("[DOOR] Manual open\r\n");
    }
    else if (btn == BTN_EVENT_DOOR_CLOSE)
    {
        Buzzer_Dong();
        Servo_Door_Close();
        printf("[DOOR] Manual close\r\n");
    }

    if (target_floor != current_floor)
    {
        Door_EnterPhase(DOOR_PHASE_OPENING);
        s_state = ELEV_STATE_DOOR_OPEN;
        printf("[DEPART] Door opening. target=%d\r\n", target_floor);
    }
}

static void FSM_HandleMoving(void)
{
    if (HAL_GetTick() - s_move_start_tick >= s_move_timeout_ms)
    {
        move_timeout_active = 1;
        printf("[ERROR] E201 move timeout\r\n");
        return;
    }

    if (current_floor == target_floor)
    {
        /* E301: 1<->3 왕복인데 2층 센서를 못 거쳤으면 층 건너뜀 */
        if (Is_TwoFloorTrip(s_depart_floor, target_floor) && !floor2_transit_flag)
        {
            floor_skip_active = 1;
            printf("[ERROR] E301 floor skip detected (depart=%d target=%d)\r\n",
                   s_depart_floor, target_floor);
            return;
        }

        FND_Shift_Data(current_floor);
        LED_Bar_Arrive();
        Door_EnterPhase(DOOR_PHASE_SETTLE);
        s_state = ELEV_STATE_DOOR_OPEN;
        printf("[ARRIVE] Settling. floor=%d\r\n", current_floor);
    }
}

static void FSM_HandleDoorOpen(void)
{
    uint32_t elapsed = HAL_GetTick() - s_phase_tick;

    switch (s_door_phase)
    {
        case DOOR_PHASE_SETTLE:
            if (elapsed >= SETTLE_MS)
            {
                Door_EnterPhase(DOOR_PHASE_OPENING);
                printf("[ARRIVE] Door opening\r\n");
            }
            break;

        case DOOR_PHASE_OPENING:
            if (elapsed >= DOOR_MOVE_MS)
            {
                Door_EnterPhase(DOOR_PHASE_DWELL);
                printf("[DOOR] Open. Wait %dms\r\n", BOARD_WAIT_MS);
            }
            break;

        case DOOR_PHASE_DWELL:
            if (elapsed >= BOARD_WAIT_MS)
            {
                Door_EnterPhase(DOOR_PHASE_CLOSING);
                printf("[DOOR] Closing\r\n");
            }
            break;

        case DOOR_PHASE_CLOSING:
            if (elapsed >= DOOR_MOVE_MS)
            {
                if (target_floor != current_floor)
                {
                    LED_Bar_Depart();
                    s_move_start_tick   = HAL_GetTick();
                    s_depart_floor      = current_floor;
                    floor2_transit_flag = 0;
                    s_move_timeout_ms = Is_TwoFloorTrip(current_floor, target_floor)
                                         ? MOVE_TIMEOUT_2F_MS : MOVE_TIMEOUT_1F_MS;
                    if (target_floor > current_floor)
                    {
                        Motor_Start(DIR_CW);
                        LED_Bar_Go_Up();
                    }
                    else
                    {
                        Motor_Start(DIR_CCW);
                        LED_Bar_Go_Down();
                    }
                    s_state = ELEV_STATE_MOVING;
                    printf("[DEPART] Motor started! target=%d\r\n", target_floor);
                }
                else
                {
                    s_state = ELEV_STATE_IDLE;
                    printf("[ARRIVE] Sequence done. Ready.\r\n");
                }
            }
            break;

        default:
            break;
    }
}

static void FSM_HandleError(void)
{
    if (!emergency_active && !floor_skip_active && !move_timeout_active)
    {
        Buzzer_Dong();
        Servo_Door_Close();
        target_floor = current_floor;   /* 자동 재출발 방지 (필수) */
        s_state = ELEV_STATE_IDLE;
        printf("[EMERGENCY] Cleared. Ready.\r\n");
    }
}

static void FSM_HandleInspection(void)
{
    if (!inspection_active)
    {
        Buzzer_Dong();
        Servo_Door_Close();
        target_floor = current_floor;   /* 자동 재출발 방지 (필수) */
        s_state = ELEV_STATE_IDLE;
        printf("[INSPECTION] Cleared. Ready.\r\n");
    }
}

/* 오류코드 우선순위: 낮은 우선순위부터 세팅 후 높은 게 덮어씀. 물리 비상정지(401)가 항상 최종 승자 */
static uint16_t Compute_Error_Code(void)
{
    uint16_t code = 0;

    if (!s_temp_alarm_latched && DHT_GetTemperature() >= TEMP_ALARM_ON_C)
        s_temp_alarm_latched = 1;
    else if (s_temp_alarm_latched && DHT_GetTemperature() <= TEMP_ALARM_OFF_C)
        s_temp_alarm_latched = 0;
    if (s_temp_alarm_latched) code = 101;   /* 고온 경고 */

    DHT_Status_t dht_status = DHT_GetLastStatus();
    if (dht_status == 1) code = 102;   /* DHT 타임아웃 */
    if (dht_status == 2) code = 103;   /* DHT 체크섬오류 */

    if (move_timeout_active) code = 201;   /* 이동 타임아웃 */
    if (floor_skip_active)   code = 301;   /* 층 건너뜀 */
    if (inspection_active)   code = 402;   /* 점검모드 */
    if (emergency_active)    code = 401;   /* 물리 비상정지 - 최우선 */

    return code;
}

void Elevator_FSM_Init(void)
{
    s_state      = ELEV_STATE_IDLE;
    s_door_phase = DOOR_PHASE_SETTLE;
    s_phase_tick = 0;
    Display_Init();

    /* 부팅 시 실제 위치 확인 - current_floor 기본값(1)과 실제 위치 어긋남 방지 */
    uint8_t detected = Photo_DetectCurrentFloor();
    if (detected != 0)
    {
        current_floor = detected;
        target_floor  = detected;
        printf("[HOMING] Detected floor=%d at boot, synced current/target\r\n", detected);
    }
    else
    {
        printf("[HOMING] No floor sensor active at boot - keeping default floor=1\r\n");
    }
}

void Elevator_FSM_Update(void)
{
    /* 비상/점검 감지는 모든 상태보다 우선. 모터정지는 EXTI ISR에서 이미 수행(이중 확인) */
    if ((emergency_active || floor_skip_active || move_timeout_active) && s_state != ELEV_STATE_ERROR)
    {
        Motor_Stop();
        LED_Bar_Arrive();
        Buzzer_Emergency();
        Servo_Door_Open();
        Door_EnterPhase(DOOR_PHASE_SETTLE);
        s_state = ELEV_STATE_ERROR;
        printf("[EMERGENCY] Lockdown! (emergency=%d skip=%d timeout=%d)\r\n",
               emergency_active, floor_skip_active, move_timeout_active);
    }

    if (inspection_active && !emergency_active && s_state != ELEV_STATE_INSPECTION)
    {
        Motor_Stop();
        LED_Bar_Arrive();
        Buzzer_Emergency();
        Servo_Door_Open();
        Door_EnterPhase(DOOR_PHASE_SETTLE);
        s_state = ELEV_STATE_INSPECTION;
        printf("[INSPECTION] Started. System locked.\r\n");
    }

    switch (s_state)
    {
        case ELEV_STATE_IDLE:       FSM_HandleIdle();       break;
        case ELEV_STATE_MOVING:     FSM_HandleMoving();     break;
        case ELEV_STATE_DOOR_OPEN:  FSM_HandleDoorOpen();   break;
        case ELEV_STATE_ERROR:      FSM_HandleError();      break;
        case ELEV_STATE_INSPECTION: FSM_HandleInspection(); break;
        default:                    break;
    }

    s_error_code = Compute_Error_Code();

    Update_Elevator_Motor();   /* Display_Update(블로킹 I2C)보다 먼저 호출 */

    Display_Update((Display_State_t)s_state, s_error_code);
}

ElevatorState_t Elevator_FSM_GetState(void)
{
    return s_state;
}

uint16_t Elevator_FSM_GetErrorCode(void)
{
    return s_error_code;
}
