#include "app_elevator_fsm.h"

#include <stdio.h>
#include "dev_display.h"
#include "dev_button.h"
#include "dev_servo.h"
#include "dev_stepper.h"
#include "dev_buzzer.h"
#include "dev_photo.h"
#include "dev_dht.h"

/* ======================================================
 * [이 파일의 역할]
 * 엘리베이터 전체 동작을 상태(FSM)로 관리한다.
 * Elevator_FSM_Update()가 메인루프에서 매번 호출되며,
 * 내부에서 절대 대기(HAL_Delay)하지 않고 HAL_GetTick() 경과시간만 확인한다.
 * (메인루프가 멈추면 IWDG 워치독 리셋이 걸리기 때문)
 *
 * 상태 전이 큰 흐름:
 *   IDLE ─(버튼으로 목표층 지정)→ DOOR_OPEN(탑승) ─(문 닫힘)→ MOVING
 *        ←(문 닫힘, 목표=현재층)─ DOOR_OPEN(하차) ←(목표층 도착)─┘
 * ====================================================== */

/* ── main.c의 전역 상태 플래그 (ISR·RS-485·FSM이 함께 갱신) ── */
extern volatile uint8_t move_timeout_active;   /* 이동 타임아웃 발생 (오류코드 E201) */
extern volatile uint8_t floor_skip_active;     /* 층 건너뜀 감지   (오류코드 E301) */
extern volatile uint8_t floor2_transit_flag;   /* 이번 이동 중 2층 센서를 지나쳤는지 (E301 판정용) */

#define DOOR_MOVE_MS   2000   /* 서보가 문을 완전히 열거나 닫는 데 걸리는 시간 */
#define BOARD_WAIT_MS  3000   /* 문을 열어둔 채 승객이 타고 내리기를 기다리는 시간 */
#define SETTLE_MS      3000   /* 도착 직후 흔들림이 가라앉기를 기다렸다가 문을 여는 시간 */

/* DOOR_OPEN 상태 내부에서만 쓰는 세부 단계.
 * 헤더에 내보내지 않아 다른 파일에서는 보이지 않는다 */
typedef enum {
    DOOR_PHASE_SETTLE = 0,
    DOOR_PHASE_OPENING,
    DOOR_PHASE_DWELL,
    DOOR_PHASE_CLOSING
} DoorPhase_t;

static ElevatorState_t s_state      = ELEV_STATE_IDLE;
static DoorPhase_t     s_door_phase = DOOR_PHASE_SETTLE;
static uint32_t        s_phase_tick = 0;

/* 이동 타임아웃(E201) 기준값 — 스텝모터 실제 주행시간을 측정해 정한 값.
 * 한 층 이동에 걸리는 실측 시간에 여유를 둔 값이며, 두 층은 그 2배 */
#define MOVE_TIMEOUT_1F_MS  60000    /* 한 층 이동 (1<->2, 2<->3) */
#define MOVE_TIMEOUT_2F_MS  120000   /* 두 층 이동 (1<->3) */

/* 고온 경고(E101) 히스테리시스 — 31도에서 켜지고 28도로 내려가야 꺼진다.
 * 한 기준값만 쓰면 경계 근처에서 경고가 깜빡이며 반복 발생하기 때문 */
#define TEMP_ALARM_ON_C   31.0f
#define TEMP_ALARM_OFF_C  28.0f

/* 1<->3층처럼 중간에 2층을 반드시 거치는 이동인지 판정.
 * E201 타임아웃 기준값 선택과 E301 층 건너뜀 판정에 공용으로 쓴다 */
static inline uint8_t Is_TwoFloorTrip(uint8_t from, uint8_t to)
{
    return (from == 1 && to == 3) || (from == 3 && to == 1);
}

static uint32_t s_move_start_tick    = 0;                    /* 출발 시각 (타임아웃 계산 기준) */
static uint8_t  s_depart_floor       = 1;                    /* 출발한 층 (E301 판정용) */
static uint32_t s_move_timeout_ms    = MOVE_TIMEOUT_1F_MS;   /* 이번 이동에 적용할 타임아웃 */
static uint8_t  s_temp_alarm_latched = 0;                    /* 고온 경고 래치 (E101 히스테리시스) */
static uint16_t s_error_code         = 0;                    /* 현재 오류코드 (0 = 정상) */

/* ======================================================
 * 문 단계 전환 — 단계를 바꾸면서 시작 시각을 기록하고,
 * 그 단계에서 한 번만 해야 할 동작(부저·서보)을 실행한다
 * ====================================================== */
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

/* ======================================================
 * IDLE — 버튼 입력을 받는다.
 * 목표층이 현재층과 달라지면 곧바로 출발 준비(문 열기)로 넘어간다
 * ====================================================== */
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

/* ======================================================
 * MOVING — 모터 구동 중.
 * current_floor는 포토센서 인터럽트가 갱신하고, 목표층 도착 시
 * 모터 정지도 인터럽트에서 이미 끝나 있다. 여기서는 도착 여부 확인과
 * 안전 검사(E201 타임아웃 / E301 층 건너뜀)만 담당한다
 * ====================================================== */
static void FSM_HandleMoving(void)
{
    /* 정해진 시간 안에 도착하지 못하면 이동 타임아웃 — 모터 걸림·센서 고장 의심 */
    if (HAL_GetTick() - s_move_start_tick >= s_move_timeout_ms)
    {
        move_timeout_active = 1;
        printf("[ERROR] E201 move timeout\r\n");
        return;
    }

    if (current_floor == target_floor)
    {
        /* E301: 1<->3 이동이면 반드시 2층 센서를 지나야 한다.
         * 그 기록이 없는데 도착했다면 센서를 놓친 것이므로 층 건너뜀으로 본다 */
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

/* ======================================================
 * DOOR_OPEN — 문 동작을 4단계로 순서대로 진행한다.
 *   SETTLE  : 도착 흔들림이 가라앉기를 기다림 (출발 시에는 건너뜀)
 *   OPENING : 문 열리는 중
 *   DWELL   : 열어둔 채 탑승/하차 대기
 *   CLOSING : 문 닫히는 중 → 닫히면 목표층에 따라 출발하거나 IDLE로 복귀
 * 각 단계는 HAL_Delay 없이 경과시간만 비교해서 넘어간다
 * ====================================================== */
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
                /* 목표층이 남아 있으면 출발, 아니면 도착 절차가 끝난 것 */
                if (target_floor != current_floor)
                {
                    /* 출발 직전에 안전 검사용 기준값들을 초기화한다 */
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

/* ======================================================
 * ERROR — 잠금 상태. 세 원인(비상정지·층 건너뜀·이동 타임아웃)이
 * 모두 사라져야 해제된다. 해제 조건 확인 외에는 아무 동작도 하지 않는다
 * ====================================================== */
static void FSM_HandleError(void)
{
    if (!emergency_active && !floor_skip_active && !move_timeout_active)
    {
        Buzzer_Dong();
        Servo_Door_Close();
        /* 목표층을 현재층으로 맞춰 둔다. 이걸 빼먹으면 해제 직후 IDLE에서
         * 목표층이 남아 있다고 판단해 승객 의사와 무관하게 다시 출발한다 */
        target_floor = current_floor;
        s_state = ELEV_STATE_IDLE;
        printf("[EMERGENCY] Cleared. Ready.\r\n");
    }
}

/* ======================================================
 * INSPECTION — 관제실이 RS-485로 켠 점검 모드.
 * 관제실이 점검을 끄면 해제된다 (해제 처리는 ERROR와 동일)
 * ====================================================== */
static void FSM_HandleInspection(void)
{
    if (!inspection_active)
    {
        Buzzer_Dong();
        Servo_Door_Close();
        target_floor = current_floor;   /* 해제 직후 자동 재출발 방지 (필수) */
        s_state = ELEV_STATE_IDLE;
        printf("[INSPECTION] Cleared. Ready.\r\n");
    }
}

/* ======================================================
 * 오류코드 계산 (매 tick 실행, 결과는 LCD와 관제실에 그대로 전달)
 *
 * 코드 체계 — 앞자리가 심각도, 아래로 갈수록 우선순위가 높다
 *   1xx 환경/센서 : 101 고온 경고 / 102 DHT 응답없음 / 103 DHT 체크섬오류
 *   2xx 구동      : 201 이동 타임아웃 (정해진 시간 안에 도착 못 함)
 *   3xx 위치      : 301 층 건너뜀 (거쳐야 할 층 센서를 못 지남)
 *   4xx 운행정지  : 402 점검 모드 / 401 물리 비상정지 버튼
 *
 * 낮은 우선순위부터 code에 넣고 높은 것이 덮어쓰는 방식이라,
 * 마지막에 검사하는 물리 비상정지(401)가 항상 최종 승자가 된다
 * ====================================================== */
static uint16_t Compute_Error_Code(void)
{
    uint16_t code = 0;

    /* 고온 경고는 히스테리시스로 래치 — 켜짐 31도 이상, 꺼짐 28도 이하 */
    if (!s_temp_alarm_latched && DHT_GetTemperature() >= TEMP_ALARM_ON_C)
        s_temp_alarm_latched = 1;
    else if (s_temp_alarm_latched && DHT_GetTemperature() <= TEMP_ALARM_OFF_C)
        s_temp_alarm_latched = 0;
    if (s_temp_alarm_latched) code = 101;   /* 고온 경고 */

    DHT_Status_t dht_status = DHT_GetLastStatus();
    if (dht_status == 1) code = 102;   /* DHT 응답 없음 (DHT_TIMEOUT) */
    if (dht_status == 2) code = 103;   /* DHT 체크섬 오류 (DHT_CHECKSUM_ERROR) */

    if (move_timeout_active) code = 201;   /* 이동 타임아웃 */
    if (floor_skip_active)   code = 301;   /* 층 건너뜀 */
    if (inspection_active)   code = 402;   /* 점검 모드 */
    if (emergency_active)    code = 401;   /* 물리 비상정지 — 최우선 */

    return code;
}

/* ======================================================
 * 초기화 — 부팅 시 1회 호출
 * ====================================================== */
void Elevator_FSM_Init(void)
{
    s_state      = ELEV_STATE_IDLE;
    s_door_phase = DOOR_PHASE_SETTLE;
    s_phase_tick = 0;
    Display_Init();

    /* 부팅 시 포토센서를 직접 읽어 실제 위치를 확인한다.
     * 이 과정이 없으면 2층·3층에서 전원을 켰을 때 소프트웨어는 1층이라고
     * 믿게 되어, 첫 이동에서 엉뚱한 방향으로 출발한다 */
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

/* ======================================================
 * 메인 갱신 — 메인루프에서 매번 호출 (논블로킹)
 * 순서: 1) 비상 감지  2) 점검 감지  3) 현재 상태 처리
 *       4) 오류코드 계산  5) 모터 스텝  6) 화면 갱신
 * ====================================================== */
void Elevator_FSM_Update(void)
{
    /* 비상 상황은 어떤 상태보다 먼저 검사한다.
     * 비상정지 버튼(PA8)일 때는 EXTI 인터럽트가 이미 모터를 껐으므로 여기서는
     * 확인 사살이지만, 층 건너뜀·이동 타임아웃은 이 함수가 유일한 정지 지점이다 */
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

    /* 점검 모드 진입. 비상 상황이 함께 걸려 있으면 ERROR가 우선이므로 들어가지 않는다 */
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

    /* 모터 스텝을 화면 갱신보다 먼저 처리한다.
     * Display_Update()의 LCD 출력은 I2C 전송이 끝날 때까지 기다리는 방식이라
     * 뒤에 두면 모터 스텝 간격이 밀려 움직임이 끊기기 때문 */
    Update_Elevator_Motor();

    /* FSM 상태를 그대로 캐스팅해 전달 (두 enum의 값 순서가 동일하도록 맞춰 둠) */
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
