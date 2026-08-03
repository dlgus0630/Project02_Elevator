#ifndef INC_DEV_DISPLAY_H_
#define INC_DEV_DISPLAY_H_

#include "main.h"

/* main.c의 전역 상태 변수 (ISR/FSM에서 갱신) */
extern volatile uint8_t current_floor;
extern volatile uint8_t target_floor;
extern volatile uint8_t emergency_active;
extern volatile uint8_t inspection_active;

/* =========================================================
 * 1. LED 바 (8-LED, 74HC595) — PA7(SER) / PB6(SRCLK) / PC7(RCLK)
 *    ※ SRCLK: PB6 -> PC10 -> PB6 (FND가 PB3~5로 옮겨가며 재확보)
 * ========================================================= */
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

/* =========================================================
 * 2. FND (7-Segment, 74HC595) — PB5(SER) / PB4(SRCLK) / PB3(RCLK)
 *    ※ PB9/PB8/PC9 -> PB5/PB4/PB3 (I2C1을 PB8/PB9로 옮기며 재배치)
 * ========================================================= */
void FND_Shift_Data(uint8_t data);
void FND_Scan(void);

/* =========================================================
 * 3. RGB 운행 상태 LED — PA0(R) / PA1(G) / PA4(B), 공통캐소드 가정
 * ========================================================= */
typedef enum {
    RGB_OFF = 0,
    RGB_GREEN,   // 대기(IDLE) / 문 열림(DOOR_OPEN)
    RGB_YELLOW,  // 점검 모드(INSPECTION), R+G 혼합 — 점멸은 Display_Update가 처리
    RGB_RED,     // 에러(ERROR) — 점멸은 Display_Update가 처리
    RGB_BLUE     // 이동 중(MOVING)
} RGB_Color_t;

void RGB_Init(void);
void RGB_SetColor(RGB_Color_t color);

/* =========================================================
 * 4. 1602 LCD — I2C1 (PB8=SCL, PB9=SDA), PCF8574 백팩 사용
 * ========================================================= */
#define LCD_I2C_ADDR   (0x27 << 1)  // 백팩 기본 주소(0x27). 안 뜨면 0x3F로 변경

void LCD_Init(void);
void LCD_SetCursor(uint8_t row, uint8_t col);
void LCD_Print(const char *str);

/* =========================================================
 * 5. 통합 상태 표시 — FSM이 상태만 넘기면 RGB+LCD를 알아서 갱신
 *    (내부적으로 이전 출력과 비교해 변경분만 I2C로 전송 = 논블로킹 친화적)
 * ========================================================= */
typedef enum {
    DISP_STATE_IDLE = 0,
    DISP_STATE_MOVING,
    DISP_STATE_DOOR_OPEN,
    DISP_STATE_ERROR,
    DISP_STATE_INSPECTION
} Display_State_t;

void Display_Init(void);                 // RGB_Init + LCD_Init 한번에
void Display_Update(Display_State_t state, uint16_t error_code); // 메인루프에서 매 tick 호출(논블로킹)

/* =========================================================
 * 6. 디버그용 — I2C 버스에 실제로 응답하는 장치 주소를 UART로 출력
 *    (LCD가 안 보일 때 배선/주소 확인용, 확인 끝나면 호출부 지워도 됨)
 * ========================================================= */
void I2C_Scan(void);

#endif /* INC_DEV_DISPLAY_H_ */
