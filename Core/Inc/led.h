#ifndef INC_LED_H_
#define INC_LED_H_

#include "main.h"

extern volatile uint8_t emergency_active;

#define LED_DATA_PORT   GPIOA
#define LED_DATA_PIN    GPIO_PIN_7   // DS    (데이터)

#define LED_CLK_PORT    GPIOB
#define LED_CLK_PIN     GPIO_PIN_6   // SH_CP (클럭)

#define LED_LATCH_PORT  GPIOC
#define LED_LATCH_PIN   GPIO_PIN_7   // ST_CP (래치)

void LED_Bar_Update(void);   // 메인루프에서 매번 호출(논블로킹)
void LED_Bar_Go_Up(void);    // 상승 시작 시 호출
void LED_Bar_Go_Down(void);  // 하강 시작 시 호출
void LED_Bar_Arrive(void);   // 도착 시 호출 (3번 깜빡 후 OFF)
void LED_Bar_Depart(void);   // 출발 시 호출 (3번 깜빡 후 전체 ON)

#endif /* INC_LED_H_ */
