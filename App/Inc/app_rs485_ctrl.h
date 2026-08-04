#ifndef APP_RS485_CTRL_H_
#define APP_RS485_CTRL_H_

#include "main.h"

/* ======================================================
 * Modbus RTU 슬레이브 (엘리베이터 카 = F411)
 * 상대편: 관제실(F429) 보드가 마스터, 이 보드는 물어보면 답하는 슬레이브
 * 통신: USART1 (PA9=TX, PA10=RX), 115200bps, RS-485 반이중
 * 지원 기능코드: 0x03 (레지스터 읽기), 0x06 (단일 레지스터 쓰기)
 * ====================================================== */

#define MODBUS_SLAVE_ID   1   // 이 보드의 Modbus 주소 (마스터가 이 번호로 호출)

/* ── 레지스터 맵 (주소는 0번부터, R=읽기전용 / R+W=마스터가 쓰기 가능) ──
 *  관제실 코드와 이 표가 반드시 똑같아야 한다. 순서를 바꾸면 양쪽 다 고쳐야 함 */
typedef enum {
    REG_CURRENT_FLOOR   = 0,   // 현재층 1~3        (R)
    REG_TARGET_FLOOR    = 1,   // 목표층 1~3        (R/W) 관제실 호출 버튼이 여기에 씀
    REG_PENDING_ACTION  = 2,   // FSM 상태          (R)  0대기/1이동/2문열림/3오류/4점검
    REG_EMERGENCY       = 3,   // 비상정지 상태     (R/W, 0=정상 / 0을 쓰면 오류 잠금해제)
    REG_TEMPERATURE_X10 = 4,   // 온도*10           (R)  예: 23.5도 -> 235
    REG_HUMIDITY_X10    = 5,   // 습도*10           (R)
    REG_DHT_STATUS      = 6,   // DHT 센서 상태     (R)  0정상/1타임아웃/2체크섬오류
    REG_INSPECTION      = 7,   // 점검 모드 상태    (R/W, 0=정상, 1=점검중)
    /* 통합 오류코드 (R) — 값이 클수록 높은 우선순위로 덮어쓴 결과가 실린다
       0=정상 / 101=고온경고 / 102=DHT타임아웃 / 103=DHT체크섬오류
       201=이동 타임아웃 / 301=층 건너뜀 / 401=물리 비상정지 / 402=점검모드 */
    REG_ERROR_CODE      = 8,
} ModbusReg_t;

void Modbus_Init(void);     // 시작할 때 한 번만 호출 — UART 수신 인터럽트를 켠다
void Modbus_Update(void);   // 메인루프에서 매번 호출 — 다 받은 프레임이 있으면 처리

#endif /* APP_RS485_CTRL_H_ */
