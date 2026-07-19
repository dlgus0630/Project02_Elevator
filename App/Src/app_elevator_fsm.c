#include "app_elevator_fsm.h"

#include <stdio.h>
#include "dev_display.h"   // current_floor/target_floor/emergency_active(extern), Display_*, LED_Bar_*, FND_*
#include "dev_button.h"    // Button_Scan, ButtonEvent_t
#include "dev_servo.h"     // Servo_Door_Open/Close
#include "dev_stepper.h"   // Motor_Start/Stop, Update_Elevator_Motor, DIR_CW/CCW
#include "dev_buzzer.h"    // Buzzer_Ding/Dong/Emergency
#include "dev_photo.h"      // Photo_DetectCurrentFloor — 부팅 시 실제 위치 확인용

/* ── 시간 상수 (ms) ── */
#define DOOR_MOVE_MS   2000   // 서보가 0°↔180° 이동하는 데 걸리는 시간
#define BOARD_WAIT_MS  3000   // 문이 열린 뒤 탑승/하차 대기 시간
#define SETTLE_MS      3000   // 도착 후 관성 안정화 대기 시간

/* ── DOOR_OPEN 내부 사설 페이즈 (헤더에 노출 금지) ──
 *  탑승(출발) 경로 : OPENING → DWELL → CLOSING → (MOVING)
 *  하차(도착) 경로 : SETTLE → OPENING → DWELL → CLOSING → (IDLE)
 */
typedef enum {
    DOOR_PHASE_SETTLE = 0,   // 도착 직후 관성 안정화 대기 (문 닫힌 채)
    DOOR_PHASE_OPENING,      // 문 여는 중
    DOOR_PHASE_DWELL,        // 문 열린 채 승·하차 대기
    DOOR_PHASE_CLOSING       // 문 닫는 중
} DoorPhase_t;

/* ── FSM 내부 상태 ── */
static ElevatorState_t s_state      = ELEV_STATE_IDLE;
static DoorPhase_t     s_door_phase = DOOR_PHASE_SETTLE;
static uint32_t        s_phase_tick = 0;   // 현재 페이즈 진입 시각

/* ──────────────────────────────────────────────────────────
 * 문 개폐 페이즈 진입 — 진입 액션을 여기서 한 번만 수행
 * ────────────────────────────────────────────────────────── */
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
            /* 진입 액션 없음 — 문 상태 유지한 채 대기만 */
            break;
    }
}

/* ──────────────────────────────────────────────────────────
 * IDLE : 버튼 스캔 + 출발 트리거(target != current)
 * ────────────────────────────────────────────────────────── */
static void FSM_HandleIdle(void)
{
    ButtonEvent_t btn = Button_Scan();

    if      (btn == BTN_EVENT_FLOOR_1) target_floor = 1;
    else if (btn == BTN_EVENT_FLOOR_2) target_floor = 2;
    else if (btn == BTN_EVENT_FLOOR_3) target_floor = 3;
    else if (btn == BTN_EVENT_DOOR_OPEN)
    {
        /* 수동 문 열기 — 상태전이 없이 즉시 열기만 */
        Buzzer_Ding();
        Servo_Door_Open();
        printf("[DOOR] Manual open\r\n");
    }
    else if (btn == BTN_EVENT_DOOR_CLOSE)
    {
        /* 수동 문 닫기 — 상태전이 없이 즉시 닫기만 */
        Buzzer_Dong();
        Servo_Door_Close();
        printf("[DOOR] Manual close\r\n");
    }

    /* 출발 트리거: 버튼 또는 modbus로 target이 바뀌어 현재층과 다르면
       문 개폐 사설머신을 OPENING부터 시작(탑승 경로) → DOOR_OPEN 진입 */
    if (target_floor != current_floor)
    {
        Door_EnterPhase(DOOR_PHASE_OPENING);
        s_state = ELEV_STATE_DOOR_OPEN;
        printf("[DEPART] Door opening. target=%d\r\n", target_floor);
    }
}

/* ──────────────────────────────────────────────────────────
 * MOVING : 포토 ISR이 도착 시 이미 Motor_Stop 수행.
 *          current==target 순간을 감지 → 하차 경로(SETTLE부터)로 진입
 * ────────────────────────────────────────────────────────── */
static void FSM_HandleMoving(void)
{
    if (current_floor == target_floor)
    {
        FND_Shift_Data(current_floor);
        LED_Bar_Arrive();                     // 도착 알림: 3회 점멸 후 소등
        Door_EnterPhase(DOOR_PHASE_SETTLE);   // 하차 경로: SETTLE 페이즈부터
        s_state = ELEV_STATE_DOOR_OPEN;
        printf("[ARRIVE] Settling. floor=%d\r\n", current_floor);
    }
}

/* ──────────────────────────────────────────────────────────
 * DOOR_OPEN : 문 개폐 사설머신 진행 (SETTLE/OPENING/DWELL/CLOSING)
 *   CLOSING 완료 시 탈출 가드로 MOVING(탑승) / IDLE(하차) 결정
 * ────────────────────────────────────────────────────────── */
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
                /* 개폐 사이클 완료 → 탈출 가드 */
                if (target_floor != current_floor)
                {
                    /* 탑승 경로: 모터 출발 → MOVING */
                    LED_Bar_Depart();
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
                    /* 하차 경로: 문 닫힘 유지한 채 IDLE 복귀 */
                    s_state = ELEV_STATE_IDLE;
                    printf("[ARRIVE] Sequence done. Ready.\r\n");
                }
            }
            break;

        default:
            break;
    }
}

/* ──────────────────────────────────────────────────────────
 * ERROR : 관리실이 비상 해제(emergency_active==0)하면 IDLE 복귀.
 *   자동 재출발 방지를 위해 target_floor를 current_floor로 강제 리셋.
 * ────────────────────────────────────────────────────────── */
static void FSM_HandleError(void)
{
    if (!emergency_active)
    {
        Buzzer_Dong();                  // 문 닫힘 안내음
        Servo_Door_Close();             // 비상 해제 시 문 닫기
        target_floor = current_floor;   // ★ 자동 재출발 방지 (필수)
        s_state = ELEV_STATE_IDLE;
        printf("[EMERGENCY] Cleared. Ready.\r\n");
    }
}

/* ──────────────────────────────────────────────────────────
 * INSPECTION : 관리실이 점검 해제(inspection_active==0)하면 IDLE 복귀.
 *   ERROR와 동일하게, 자동 재출발 방지를 위해 target_floor를 current_floor로
 *   강제 리셋한다.
 * ────────────────────────────────────────────────────────── */
static void FSM_HandleInspection(void)
{
    if (!inspection_active)
    {
        Buzzer_Dong();                  // 문 닫힘 안내음
        Servo_Door_Close();             // 점검 해제 시 문 닫기
        target_floor = current_floor;   // ★ 자동 재출발 방지 (필수)
        s_state = ELEV_STATE_IDLE;
        printf("[INSPECTION] Cleared. Ready.\r\n");
    }
}

/* ==========================================================
 * 공개 API
 * ========================================================== */
void Elevator_FSM_Init(void)
{
    s_state      = ELEV_STATE_IDLE;
    s_door_phase = DOOR_PHASE_SETTLE;
    s_phase_tick = 0;
    Display_Init();                     // RGB_Init + LCD_Init

    /* 부팅 시 실제 위치 확인 — 모형이 2층/3층에 놓인 채 전원이 켜져도
       current_floor(기본값 1)가 실제 위치와 어긋나지 않도록 동기화한다. */
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
    /* 1) 비상 감지 — 모든 상태에서 최우선.
     *    모터 정지는 EXTI ISR에서 이미 수행되었으나 이중 확인.
     *    ERROR 진입 엣지에서만 연출/부저를 1회 실행. */
    if (emergency_active && s_state != ELEV_STATE_ERROR)
    {
        Motor_Stop();
        LED_Bar_Arrive();               // led.c의 emergency 분기가 점멸 처리
        Buzzer_Emergency();             // "띵" 3회 연속 비상음
        Servo_Door_Open();              // 모터 정지 후 안전을 위해 문 자동 개방
        Door_EnterPhase(DOOR_PHASE_SETTLE);   // 사설머신 리셋
        s_state = ELEV_STATE_ERROR;
        printf("[EMERGENCY] Stop button pressed!\r\n");
    }

    /* 1-2) 점검 모드 감지 — 관리실 원격 명령(inspection_active)으로 진입.
     *      단, 물리적 비상정지가 최우선이므로 !emergency_active일 때만 반응한다.
     *      INSPECTION 진입 엣지에서만 연출/부저를 1회 실행. */
    if (inspection_active && !emergency_active && s_state != ELEV_STATE_INSPECTION)
    {
        Motor_Stop();
        LED_Bar_Arrive();
        Buzzer_Emergency();
        Servo_Door_Open();              // 모터 정지 후 안전을 위해 문 자동 개방
        Door_EnterPhase(DOOR_PHASE_SETTLE);
        s_state = ELEV_STATE_INSPECTION;
        printf("[INSPECTION] Started. System locked.\r\n");
    }

    /* 2) 상태별 처리 */
    switch (s_state)
    {
        case ELEV_STATE_IDLE:       FSM_HandleIdle();     break;
        case ELEV_STATE_MOVING:     FSM_HandleMoving();   break;
        case ELEV_STATE_DOOR_OPEN:  FSM_HandleDoorOpen(); break;
        case ELEV_STATE_ERROR:      FSM_HandleError();    break;
        case ELEV_STATE_INSPECTION: FSM_HandleInspection(); break;
        default:                    break;
    }

    /* 3) 모터 논블로킹 스텝 — Display_Update(블로킹 I2C)보다 먼저 호출 */
    Update_Elevator_Motor();

    /* 4) 통합 상태 표시 (RGB + LCD). enum 값이 Display_State_t와 일치 */
    Display_Update((Display_State_t)s_state);
}

ElevatorState_t Elevator_FSM_GetState(void)
{
    return s_state;
}
