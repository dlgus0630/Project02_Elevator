#ifndef INC_MODBUS_H_
#define INC_MODBUS_H_

#include "main.h"

/* ======================================================
 * Modbus RTU 슬레이브 (엘리베이터 카 = F411)
 * 통신: USART1 (PA9=TX, PA10=RX), 115200bps
 * 지원 기능코드: 0x03 (레지스터 읽기), 0x06 (단일 레지스터 쓰기)
 * ====================================================== */

#define MODBUS_SLAVE_ID   1   // 이 보드의 Modbus 주소

/* ── 레지스터 주소 (0번부터 시작) ── */
typedef enum {
    REG_CURRENT_FLOOR   = 0,   // 현재층            (R)
    REG_TARGET_FLOOR    = 1,   // 목표층            (R/W)
    REG_PENDING_ACTION  = 2,   // 진행 단계         (R)
    REG_EMERGENCY       = 3,   // 비상정지 상태     (R/W, 0=정상)
    REG_TEMPERATURE_X10 = 4,   // 온도*10           (R)  예: 23.5도 -> 235
    REG_HUMIDITY_X10    = 5,   // 습도*10           (R)
    REG_DHT_STATUS      = 6,   // DHT 상태          (R)  0정상/1타임아웃/2체크섬오류
} ModbusReg_t;

void Modbus_Init(void);     // 한 번 호출 — UART 수신 인터럽트 시작
void Modbus_Update(void);   // 메인루프에서 매번 호출

#endif /* INC_MODBUS_H_ */
