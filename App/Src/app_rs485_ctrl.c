#include "app_rs485_ctrl.h"
#include "usart.h"
#include "dev_dht.h"
#include "app_elevator_fsm.h"
#include <string.h>
#include <stdio.h>

/* ======================================================
 * [동작 원리]
 * 1) USART1 인터럽트로 1바이트씩 계속 받아서 버퍼에 쌓음
 * 2) 메인루프(Modbus_Update)에서 "마지막 바이트로부터 5ms 이상
 *    아무것도 안 왔으면" 그걸 "프레임이 끝났다"고 보고 해석함
 *    (Modbus RTU는 원래 프레임 사이의 침묵 구간으로 프레임을 구분하는
 *     방식이라, 이 방법이 표준 동작과 같은 효과를 낸다)
 * 3) 슬레이브 주소·CRC 확인 후, 0x03(읽기)이면 값을 돌려주고,
 *    0x06(쓰기)이면 값을 반영하고 받은 요청을 그대로 에코 응답
 *
 * 인터럽트에서는 버퍼에 담기만 하고 해석은 메인루프에서 한다.
 * ISR 안에서 응답 송신(HAL_Delay 포함)까지 하면 다른 인터럽트가
 * 밀려 층 감지 센서를 놓칠 수 있기 때문.
 * ====================================================== */

/* ── 엘리베이터 상태 원본은 main.c에 있고, 여기서는 읽고/쓰기만 한다
      (ISR이 바꾸는 값이라 전부 volatile) ── */
extern volatile uint8_t current_floor;
extern volatile uint8_t target_floor;
extern volatile uint8_t emergency_active;
extern volatile uint8_t inspection_active;
extern volatile uint8_t floor_skip_active;
extern volatile uint8_t move_timeout_active;

/* ── 수신 버퍼 ──
      쓰이는 요청 프레임은 최대 8바이트라 32바이트면 충분하다.
      넘치면 그 바이트는 그냥 버려지고, 어차피 CRC가 안 맞아 무시된다 ── */
#define RX_BUF_SIZE   32
static uint8_t  s_rx_buf[RX_BUF_SIZE];
static uint8_t  s_rx_len = 0;
static uint32_t s_last_byte_tick = 0;   /* 마지막 바이트가 들어온 시각 (프레임 끝 판정용) */
static uint8_t  s_rx_byte;              /* 인터럽트로 1바이트씩 받아두는 임시 칸 */

/* ======================================================
 * 1. CRC16 계산 (Modbus RTU 표준)
 *    초깃값 0xFFFF, 다항식 0xA001(0x8005를 비트 뒤집은 값)로
 *    LSB부터 처리하는 방식 — 규격에 정해진 그대로다.
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
 * 2. 레지스터 읽기: 주소 -> 현재 값
 *    표에 없는 주소를 물어보면 오류 대신 0을 돌려준다.
 *    (마스터가 여러 개를 한 번에 읽을 때 중간에 끊기지 않게 하려는 것)
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
 * 3. 레지스터 쓰기: 주소 + 값 -> 실제 변수에 반영
 *    성공하면 1, 실패(쓰기 금지 주소 / 허용범위 밖 값)면 0을 돌려준다.
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
            return 0;   /* 이 카는 1~3층뿐이라 그 밖의 값은 거부 */

        case REG_EMERGENCY:
            emergency_active = (value != 0) ? 1 : 0;
            if (value == 0)
            {
                /* 관제실의 "비상해제" 버튼을 오류 전체 잠금해제로도 쓴다.
                   버튼을 따로 두지 않으려고, 층 건너뜀(301)/이동 타임아웃(201)
                   래치도 여기서 같이 풀어준다 */
                floor_skip_active   = 0;
                move_timeout_active = 0;
            }
            return 1;

        case REG_INSPECTION:
            inspection_active = (value != 0) ? 1 : 0;
            return 1;

        default:
            return 0;   /* 나머지는 센서·상태 값이라 읽기 전용 (쓰기 금지) */
    }
}

/* ======================================================
 * 4. 응답 프레임 전송 — 뒤에 CRC 2바이트를 붙여서 송신
 *    resp 배열은 len+2 바이트가 들어갈 자리가 있어야 한다.
 * ====================================================== */
static void Send_Response(uint8_t *resp, uint16_t len)
{
    /* Modbus는 CRC를 하위바이트 먼저(리틀엔디안) 보낸다.
       나머지 값들은 상위바이트 먼저인 것과 반대라 헷갈리기 쉬움 */
    uint16_t crc = CRC16(resp, len);
    resp[len]     = (uint8_t)(crc & 0xFF);
    resp[len + 1] = (uint8_t)(crc >> 8);

    /* [지연을 주는 이유]
       RS-485는 선 한 쌍을 송/수신이 번갈아 쓰는 반이중 방식이다.
       요청을 받자마자 곧바로 응답을 쏘면, 마스터(관제실) 쪽 자동
       방향전환 모듈이 아직 송신 모드에서 수신 모드로 다 돌아오지
       못해 우리 응답의 앞부분을 놓친다.
       마스터의 응답 대기 한계가 100ms이므로 30ms는 충분히 안전한 값 */
    HAL_Delay(30);

    HAL_UART_Transmit(&huart1, resp, len + 2, 100);
}

/* ======================================================
 * 5. 받은 프레임 해석 + 처리
 *    프레임 구조: [슬레이브주소][기능코드][주소 2바이트][값/개수 2바이트][CRC 2바이트]
 * ====================================================== */
static void Process_Frame(uint8_t *buf, uint8_t len)
{
    if (len < 8) return;                          /* 0x03·0x06 요청은 정확히 8바이트, 그보다 짧으면 깨진 것 */
    if (buf[0] != MODBUS_SLAVE_ID) return;         /* 다른 슬레이브에게 간 요청이면 무시 */

    /* CRC 확인 — 잡음으로 한 비트라도 틀어졌으면 여기서 걸러진다
       (마지막 2바이트가 CRC이고, 하위바이트가 먼저 온다) */
    uint16_t recv_crc = (uint16_t)((buf[len - 1] << 8) | buf[len - 2]);
    uint16_t calc_crc = CRC16(buf, len - 2);
    if (recv_crc != calc_crc) return;              /* CRC 틀리면 무시 */

    uint8_t  func = buf[1];
    uint16_t addr = (uint16_t)((buf[2] << 8) | buf[3]);

    if (func == 0x03)   /* ── 레지스터 읽기 (Read Holding Registers) ── */
    {
        uint16_t qty = (uint16_t)((buf[4] << 8) | buf[5]);
        if (qty == 0 || qty > 16) return;          /* 아래 resp 배열이 16개까지만 감당하므로 그 이상은 거부 */

        uint8_t resp[3 + 32 + 2];                  /* 머리 3 + 값 최대 16개*2 + CRC 2 */
        resp[0] = MODBUS_SLAVE_ID;
        resp[1] = 0x03;
        resp[2] = (uint8_t)(qty * 2);              /* 뒤에 따라올 데이터의 바이트 수 */

        for (uint16_t i = 0; i < qty; i++)
        {
            uint16_t val = Get_Register(addr + i);
            resp[3 + i * 2]     = (uint8_t)(val >> 8);
            resp[3 + i * 2 + 1] = (uint8_t)(val & 0xFF);
        }
        Send_Response(resp, 3 + qty * 2);
    }
    else if (func == 0x06)   /* ── 단일 레지스터 쓰기 (Write Single Register) ── */
    {
        uint16_t value = (uint16_t)((buf[4] << 8) | buf[5]);

        if (Set_Register(addr, value))
        {
            /* 쓰기가 정상 처리되면 받은 요청 6바이트를 그대로 되돌려준다
               (Modbus 0x06의 규정된 응답 형식) */
            uint8_t resp[6];
            memcpy(resp, buf, 6);
            Send_Response(resp, 6);
        }
        /* 실패하면 아무 응답도 보내지 않는다. 마스터는 100ms 뒤 타임아웃으로
           실패를 알아채고 재시도한다 (예외응답 0x86은 아직 구현 안 함) */
    }
}

/* ======================================================
 * 6. UART 수신 인터럽트 콜백 — 1바이트 받을 때마다 HAL이 불러준다
 *    (HAL이 미리 만들어 둔 빈 함수를 여기서 덮어쓰는 구조)
 * ====================================================== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    /* 이 콜백은 모든 UART가 공유하므로, USART1(Modbus)이 아니면 그냥 나간다
       (USART2는 디버그 출력용이라 여기서 처리하지 않는다) */
    if (huart->Instance != USART1) return;

    if (s_rx_len < RX_BUF_SIZE)
    {
        s_rx_buf[s_rx_len++] = s_rx_byte;
        s_last_byte_tick = HAL_GetTick();
    }

    /* 인터럽트 수신은 1바이트마다 자동으로 꺼지므로, 여기서 다시 걸어줘야
       다음 바이트를 계속 받는다. 이걸 빼먹으면 통신이 한 번에 멈춘다 */
    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
}

/* ======================================================
 * 7. 공개 API (다른 파일에서 부르는 함수)
 * ====================================================== */
void Modbus_Init(void)
{
    s_rx_len = 0;
    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);   /* 첫 1바이트 수신 대기 시작 */
}

/* 마지막 바이트로부터 5ms 넘게 조용하면 "프레임이 끝났다"고 보고 처리한다.
   115200bps에서 1바이트는 약 0.09ms이므로, 5ms 공백은 프레임 중간에서는
   생길 수 없는 간격이라 경계 판정 기준으로 안전하다 */
void Modbus_Update(void)
{
    if (s_rx_len == 0) return;   /* 받은 게 없으면 할 일 없음 */

    if (HAL_GetTick() - s_last_byte_tick >= 5)
    {
        Process_Frame(s_rx_buf, s_rx_len);
        s_rx_len = 0;
    }
}
