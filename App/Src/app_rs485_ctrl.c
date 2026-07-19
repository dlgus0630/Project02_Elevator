#include "app_rs485_ctrl.h"
#include "usart.h"
#include "dev_dht.h"
#include "app_elevator_fsm.h"
#include <string.h>
#include <stdio.h>   /* 임시 진단 printf용 */

/* ======================================================
 * [동작 원리]
 * 1) USART1 인터럽트로 1바이트씩 계속 받아서 버퍼에 쌓음
 * 2) 메인루프(Modbus_Update)에서 "마지막 바이트로부터 5ms 이상
 *    아무것도 안 왔으면" 그걸 "프레임이 끝났다"고 보고 해석함
 *    (Modbus RTU는 원래 프레임 사이에 침묵 구간으로 구분하는 방식이라
 *     이 방법이 표준 동작과 같은 효과를 냄)
 * 3) 슬레이브 주소·CRC 확인 후, 0x03(읽기)이면 값을 돌려주고,
 *    0x06(쓰기)이면 값을 반영하고 그대로 에코 응답
 * ====================================================== */

/* ── main.c에 있는 실제 변수들 (엘리베이터 상태) ── */
extern volatile uint8_t current_floor;
extern volatile uint8_t target_floor;
extern volatile uint8_t emergency_active;
extern volatile uint8_t inspection_active;
extern volatile uint8_t floor_skip_active;
extern volatile uint8_t move_timeout_active;

/* ── 수신 버퍼 ── */
#define RX_BUF_SIZE   32
static uint8_t  s_rx_buf[RX_BUF_SIZE];
static uint8_t  s_rx_len = 0;
static uint32_t s_last_byte_tick = 0;
static uint8_t  s_rx_byte;   /* 인터럽트로 1바이트씩 받는 임시 변수 */

/* ======================================================
 * CRC16 계산 (Modbus 표준 방식 - 그대로 외우지 말고 복사만 하면 됨)
 * ====================================================== */
static uint16_t CRC16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t pos = 0; pos < len; pos++)
    {
        crc ^= buf[pos];
        for (int i = 0; i < 8; i++)
        {
            if (crc & 0x0001) { crc >>= 1; crc ^= 0xA001; }
            else              { crc >>= 1; }
        }
    }
    return crc;
}

/* ======================================================
 * 레지스터 읽기: 주소 -> 실제 값
 * ====================================================== */
static uint16_t Get_Register(uint16_t addr)
{
    switch (addr)
    {
        case REG_CURRENT_FLOOR:   return current_floor;
        case REG_TARGET_FLOOR:    return target_floor;
        case REG_PENDING_ACTION:  return (uint16_t)Elevator_FSM_GetState();
        case REG_EMERGENCY:       return emergency_active;
        case REG_TEMPERATURE_X10: return (uint16_t)(DHT_GetTemperature() * 10.0f);
        case REG_HUMIDITY_X10:    return (uint16_t)(DHT_GetHumidity()    * 10.0f);
        case REG_DHT_STATUS:      return (uint16_t)DHT_GetLastStatus();
        case REG_INSPECTION:      return inspection_active;
        case REG_ERROR_CODE:      return Elevator_FSM_GetErrorCode();
        default:                  return 0;
    }
}

/* ======================================================
 * 레지스터 쓰기: 주소 + 값 -> 실제 변수에 반영
 * 성공하면 1, 실패(잘못된 주소/값)하면 0 반환
 * ====================================================== */
static uint8_t Set_Register(uint16_t addr, uint16_t value)
{
    switch (addr)
    {
        case REG_TARGET_FLOOR:
            if (value >= 1 && value <= 3)
            {
                target_floor = (uint8_t)value;
                return 1;
            }
            return 0;   /* 1~3 층 외의 값은 거부 */

        case REG_EMERGENCY:
            emergency_active = (value != 0) ? 1 : 0;
            if (value == 0)
            {
                /* "비상해제" 버튼을 범용 잠금해제로도 사용 —
                   위치인식오류(E301)/이동타임아웃(E201)도 같이 푼다 */
                floor_skip_active   = 0;
                move_timeout_active = 0;
            }
            return 1;

        case REG_INSPECTION:
            inspection_active = (value != 0) ? 1 : 0;
            return 1;

        default:
            return 0;   /* 그 외 레지스터는 쓰기 금지 (읽기 전용) */
    }
}

/* ======================================================
 * 응답 프레임 전송 (CRC 붙여서 송신)
 * ====================================================== */
static void Send_Response(uint8_t *resp, uint16_t len)
{
    uint16_t crc = CRC16(resp, len);
    resp[len]     = (uint8_t)(crc & 0xFF);
    resp[len + 1] = (uint8_t)(crc >> 8);

    /* 요청을 받자마자(수 ms 이내) 바로 응답을 쏘면, 마스터(관리실) 쪽
       자동 방향전환 모듈이 "자기가 보낸 요청"의 송신 여운(수신 모드로
       완전히 안 돌아온 상태)에 아직 갇혀 있어서 응답을 놓칠 수 있다.
       마스터 타임아웃(100ms)에 여유가 있으니 지연을 넉넉히 늘려서
       확인해본다. */
    HAL_Delay(30);

    HAL_UART_Transmit(&huart1, resp, len + 2, 100);
}

/* ======================================================
 * 받은 프레임 해석 + 처리
 * ====================================================== */
static void Process_Frame(uint8_t *buf, uint8_t len)
{
    if (len < 8) return;                          /* 너무 짧으면 무시 */
    if (buf[0] != MODBUS_SLAVE_ID) return;         /* 내 주소가 아니면 무시 */

    /* CRC 확인 (마지막 2바이트가 CRC) */
    uint16_t recv_crc = (uint16_t)((buf[len - 1] << 8) | buf[len - 2]);
    uint16_t calc_crc = CRC16(buf, len - 2);
    if (recv_crc != calc_crc) return;              /* CRC 틀리면 무시 */

    uint8_t  func = buf[1];
    uint16_t addr = (uint16_t)((buf[2] << 8) | buf[3]);

    /* 임시 진단: 주소/CRC 검증까지 통과한 유효한 요청인지 확인 */
    printf("[RS485 VALID] t=%lu func=%02X addr=%u\r\n",
           (unsigned long)HAL_GetTick(), func, addr);

    if (func == 0x03)   /* ── 레지스터 읽기 (Read Holding Registers) ── */
    {
        uint16_t qty = (uint16_t)((buf[4] << 8) | buf[5]);
        if (qty == 0 || qty > 16) return;          /* 너무 많이 요청하면 무시 */

        uint8_t resp[3 + 32 + 2];
        resp[0] = MODBUS_SLAVE_ID;
        resp[1] = 0x03;
        resp[2] = (uint8_t)(qty * 2);              /* 바이트 수 */

        for (uint16_t i = 0; i < qty; i++)
        {
            uint16_t val = Get_Register(addr + i);
            resp[3 + i * 2]     = (uint8_t)(val >> 8);
            resp[3 + i * 2 + 1] = (uint8_t)(val & 0xFF);
        }
        /* 임시 진단: 응답을 실제로 내보내려고 시도하는지 확인 */
        printf("[RS485 SEND] t=%lu sending %u bytes\r\n",
               (unsigned long)HAL_GetTick(), (unsigned)(3 + qty * 2 + 2));
        Send_Response(resp, 3 + qty * 2);
    }
    else if (func == 0x06)   /* ── 단일 레지스터 쓰기 (Write Single Register) ── */
    {
        uint16_t value = (uint16_t)((buf[4] << 8) | buf[5]);

        if (Set_Register(addr, value))
        {
            /* 정상 처리되면 받은 요청 그대로 에코 (Modbus 표준 동작) */
            uint8_t resp[6];
            memcpy(resp, buf, 6);
            Send_Response(resp, 6);
        }
        /* 실패 시 — 지금은 간단하게 그냥 응답 안 보냄 (예외응답은 나중에) */
    }
}

/* ======================================================
 * UART 수신 인터럽트 콜백 — 1바이트 받을 때마다 자동 호출됨
 * ====================================================== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;   /* USART1(Modbus)이 아니면 무시 */

    if (s_rx_len < RX_BUF_SIZE)
    {
        s_rx_buf[s_rx_len++] = s_rx_byte;
        s_last_byte_tick = HAL_GetTick();
    }

    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);   /* 다음 1바이트 또 받기 시작 */
}

/* ======================================================
 * 공개 API
 * ====================================================== */
void Modbus_Init(void)
{
    s_rx_len = 0;
    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);   /* 수신 인터럽트 시작 */
}

/* 메인루프에서 매번 호출 -> 5ms 이상 새 바이트가 없으면 "프레임 끝"으로 보고 처리 */
void Modbus_Update(void)
{
    if (s_rx_len == 0) return;

    if (HAL_GetTick() - s_last_byte_tick >= 5)
    {
        /* ── 임시 진단: RS485 통신 두절 원인 추적용 ──
           USART1(Modbus)에 뭐라도 도착은 하는지, 도착했다면 내용이 뭔지
           그대로 찍어본다. 관리실 쪽 [RS485 TX] 로그와 시각(t=)을 맞춰서
           실제 요청이 도착하는 건지, 그냥 버스 잡음인지 구분한다.
           원인 확인되면 반드시 지울 것. */
        printf("[RS485 RX] t=%lu len=%u raw:", (unsigned long)HAL_GetTick(), s_rx_len);
        for (uint8_t i = 0; i < s_rx_len; i++)
        {
            printf(" %02X", s_rx_buf[i]);
        }
        printf("\r\n");

        Process_Frame(s_rx_buf, s_rx_len);
        s_rx_len = 0;
    }
}
