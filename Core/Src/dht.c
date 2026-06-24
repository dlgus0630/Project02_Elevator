#include "dht.h"
#include <stdio.h>

#define READ_INTERVAL_MS   5000   // 5초(5000ms)마다 한 번씩 측정
#define WAIT_TIMEOUT_US     100   // 신호 대기 최대 시간(100us). 이 시간을 넘기면 센서 고장/오류로 판단

/* ── 마지막 측정 결과 저장용 변수들 ── */
static float        s_temperature = 0.0f;
static float        s_humidity    = 0.0f;
static DHT_Status_t s_status      = DHT_OK;
static uint32_t     s_last_read_tick = 0;

/* ======================================================
 * [1. 하드웨어 제어용 기본 함수들]
 * ====================================================== */

/* TIM5 타이머를 이용해 원하는 마이크로초(us)만큼 가만히 기다리는 함수 */
static void Delay_us(uint32_t us)
{
    uint32_t start_time = __HAL_TIM_GET_COUNTER(&htim5);

    // 현재 시간 - 시작 시간 < 목표 대기 시간일 동안 계속 무한루프(대기)
    while ((uint32_t)(__HAL_TIM_GET_COUNTER(&htim5) - start_time) < us)
    {
        // 아무것도 안 하고 시간만 보냅니다.
    }
}

/* 핀을 '출력(Output)' 모드로 설정하고, HIGH(1) 또는 LOW(0) 신호를 내보냄 */
static void Pin_Output(GPIO_PinState level)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = DHT_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(DHT_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(DHT_PORT, DHT_PIN, level);
}

/* 핀을 '입력(Input)' 모드로 설정하여 센서가 보내는 신호를 읽을 준비를 함 */
static void Pin_Input(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin  = DHT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;

    HAL_GPIO_Init(DHT_PORT, &GPIO_InitStruct);
}

/* 핀이 우리가 원하는 상태(HIGH 또는 LOW)가 될 때까지 기다리는 함수
 * 제때 바뀌면 1(성공) 반환, 너무 오래 걸리면 0(실패) 반환 */
static uint8_t Wait_For_Level(GPIO_PinState target_level, uint32_t timeout_us)
{
    uint32_t start_time = __HAL_TIM_GET_COUNTER(&htim5);

    // 핀 상태가 우리가 원하는 상태가 아닐 동안 계속 기다림
    while (HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN) != target_level)
    {
        uint32_t elapsed_time = __HAL_TIM_GET_COUNTER(&htim5) - start_time;

        // 기다린 시간이 제한시간(timeout_us)을 넘어가면 포기
        if (elapsed_time > timeout_us) {
            return 0; // 실패
        }
    }
    return 1; // 성공적으로 원하는 상태가 됨
}


/* ======================================================
 * [2. 센서 데이터 수집 핵심 로직]
 * 이 함수는 데이터를 읽어오는 과정만 담당합니다.
 * 성공하면 1, 중간에 에러가 나면 0을 반환합니다.
 * ====================================================== */
static uint8_t Read_Data_Core(uint8_t* out_data)
{
    uint8_t bits[40] = {0}; // 센서가 보내는 40개의 0과 1을 임시로 저장할 배열

    /* ── 1단계: MCU가 센서에게 "일어나서 데이터 보내!" 라고 신호 주기 ── */
    Pin_Output(GPIO_PIN_RESET);   // LOW 신호로 18ms 동안 유지 (센서 깨우기)
    HAL_Delay(18);

    Pin_Output(GPIO_PIN_SET);     // 잠깐 HIGH로 올림
    Delay_us(30);

    Pin_Input();                  // 이제 MCU는 말을 멈추고 센서의 대답을 들을(입력) 준비를 함


    /* ── 2단계: 센서가 "네, 준비됐습니다" 하고 응답하는지 확인 ── */
    // 센서는 LOW -> HIGH -> LOW 순서로 응답을 보냅니다.
    if (!Wait_For_Level(GPIO_PIN_RESET, WAIT_TIMEOUT_US)) return 0;
    if (!Wait_For_Level(GPIO_PIN_SET,   WAIT_TIMEOUT_US)) return 0;
    if (!Wait_For_Level(GPIO_PIN_RESET, WAIT_TIMEOUT_US)) return 0;


    /* ── 3단계: 본격적으로 40개의 비트(0 또는 1) 받기 ── */
    for (int i = 0; i < 40; i++)
    {
        // 데이터가 시작되려면 신호가 HIGH로 올라가야 함
        if (!Wait_For_Level(GPIO_PIN_SET, WAIT_TIMEOUT_US)) return 0;

        // HIGH 신호가 시작된 시간을 기록
        uint32_t high_start_time = __HAL_TIM_GET_COUNTER(&htim5);

        // 신호가 다시 LOW로 떨어질 때까지 기다림 (이때가 한 비트가 끝나는 시점)
        if (!Wait_For_Level(GPIO_PIN_RESET, WAIT_TIMEOUT_US)) return 0;

        // HIGH 상태로 머물렀던 시간을 계산
        uint32_t high_duration = __HAL_TIM_GET_COUNTER(&htim5) - high_start_time;

        // HIGH 유지 시간이 40us보다 길면 '1', 짧으면 '0'으로 판별
        if (high_duration > 40) {
            bits[i] = 1;
        } else {
            bits[i] = 0;
        }
    }


    /* ── 4단계: 낱개로 받은 40개의 비트를 5개의 바이트(데이터)로 조립하기 ── */
    for (int i = 0; i < 40; i++)
    {
        int byte_index = i / 8; // 0~7번째 비트는 data[0]에, 8~15번째는 data[1]에...

        // 기존 데이터를 왼쪽으로 한 칸 밀고, 빈자리에 새로 읽은 비트를 채워 넣음
        out_data[byte_index] = (out_data[byte_index] << 1) | bits[i];
    }

    return 1; // 모든 과정을 무사히 마치면 성공(1) 반환
}


/* ======================================================
 * [3. 실제 측정 및 데이터 검증 함수]
 * ====================================================== */
static void Read_Sensor_Once(void)
{
    uint8_t data[5] = {0, 0, 0, 0, 0};

    // 데이터를 읽어오는 핵심 로직 실행
    uint8_t success = Read_Data_Core(data);

    // 중요: 통신이 끝났든, 중간에 실패했든 무조건 핀 상태를 원상복구(출력 HIGH) 합니다.
    Pin_Output(GPIO_PIN_SET);

    if (!success)
    {
        s_status = DHT_TIMEOUT;
        printf("[DHT] Error: Timeout - No sensor response\r\n");
        return; // 실패했으니 함수 종료
    }

    /* ── 5단계: 통신 중 노이즈로 데이터가 깨지지 않았는지 검사(Checksum) ── */
    uint8_t calculated_checksum = data[0] + data[1] + data[2] + data[3];

    if (calculated_checksum != data[4])
    {
        s_status = DHT_CHECKSUM_ERROR;
        printf("[DHT] Error: Checksum mismatch\r\n");
        return; // 실패했으니 함수 종료
    }

    /* ── 6단계: 모든 검사가 끝났으니 온/습도 계산해서 저장 ── */
    s_humidity    = data[0] + (data[1] / 10.0f);
    s_temperature = data[2] + (data[3] / 10.0f);
    s_status      = DHT_OK;

    printf("[DHT] Temp: %.1f C, Humidity: %.1f %%\r\n", s_temperature, s_humidity);
}

/* ======================================================
 * [외부에서 가져다 쓰는 공개 API]
 * ====================================================== */

void DHT_Init(void)
{
    Pin_Output(GPIO_PIN_SET);   // 평상시에는 핀을 HIGH 상태로 둡니다.
    s_status         = DHT_OK;
    s_last_read_tick = 0;       // 시작하자마자 바로 한 번 측정하게끔 0으로 세팅
}

/* 메인 루프(while(1)) 안에서 계속 호출되는 함수 */
void DHT_Update(void)
{
    uint32_t now = HAL_GetTick();

    // 마지막으로 측정한 지 5초(READ_INTERVAL_MS)가 지났는지 확인
    if ((now - s_last_read_tick) >= READ_INTERVAL_MS)
    {
        s_last_read_tick = now; // 시간 갱신
        Read_Sensor_Once();     // 센서 측정 시작 (약 20ms 소요)
    }
}

float DHT_GetTemperature(void)       { return s_temperature; }
float DHT_GetHumidity(void)          { return s_humidity; }
DHT_Status_t DHT_GetLastStatus(void) { return s_status; }
