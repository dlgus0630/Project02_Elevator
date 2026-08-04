#ifndef INC_DHT_H_
#define INC_DHT_H_

#include "main.h"

/* DHT11 데이터 핀: PB10 (원-와이어, MCU와 센서가 한 선을 번갈아 사용) */
#define DHT_PIN      GPIO_PIN_10
#define DHT_PORT     GPIOB

/* TIM5: PSC=100-1 @100MHz → 1틱 = 1us.
 * DHT11 프로토콜은 us 단위 펄스 폭으로 0/1을 구분하므로 이 타이머가 필수다. */
extern TIM_HandleTypeDef htim5;

/* 측정 결과 상태 */
typedef enum {
    DHT_OK             = 0,   // 성공
    DHT_TIMEOUT        = 1,   // 센서 응답 없음 (선 연결/전원 확인)
    DHT_CHECKSUM_ERROR = 2    // 데이터 오류 (잡신호/노이즈)
} DHT_Status_t;

/* ── 외부에서 쓰는 함수  ── */
void         DHT_Init(void);             // 처음 한 번 초기화
void         DHT_Update(void);           // 메인루프에서 매번 호출 (내부에서 5초마다 실제 측정)

float        DHT_GetTemperature(void);   // 마지막으로 측정된 온도
float        DHT_GetHumidity(void);      // 마지막으로 측정된 습도
DHT_Status_t DHT_GetLastStatus(void);    // 마지막 측정 상태

#endif /* INC_DHT_H_ */
