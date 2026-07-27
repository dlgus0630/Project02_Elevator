#include "dev_display.h"
#include "i2c.h"     // CubeMX가 .ioc 재생성 시 만들어주는 hi2c1 핸들
#include "dev_dht.h"     // DHT_GetTemperature / DHT_GetHumidity
#include <string.h>
#include <stdio.h>

/* ── 오류코드 체계용 추가 플래그 (main.c에서 정의) ──
 *  emergency_active/inspection_active는 dev_display.h에서 extern 처리됨. */
extern volatile uint8_t floor_skip_active;     // 층 건너뜀(E301) 플래그
extern volatile uint8_t move_timeout_active;   // 이동 타임아웃(E201) 플래그

/* =========================================================
 * 1. LED 바 (74HC595) — led.c 로직 그대로 이식
 * ========================================================= */
typedef enum {
    LED_ANIM_IDLE = 0,
    LED_ANIM_DEPART_BLINK,
    LED_ANIM_UP,
    LED_ANIM_DOWN,
    LED_ANIM_ARRIVE_BLINK
} LED_Anim_t;

static LED_Anim_t s_anim      = LED_ANIM_IDLE;
static uint8_t    s_step      = 0;
static uint32_t   s_last_tick = 0;
static uint8_t    s_blink_cnt = 0;

#define RUN_INTERVAL_MS    250
#define BLINK_INTERVAL_MS  250

static void led_shift_out(uint8_t data)
{
    HAL_GPIO_WritePin(LED_LATCH_PORT, LED_LATCH_PIN, GPIO_PIN_RESET);

    for (int i = 7; i >= 0; i--)
    {
        HAL_GPIO_WritePin(LED_CLK_PORT, LED_CLK_PIN, GPIO_PIN_RESET);

        if (data & (1 << i))
            HAL_GPIO_WritePin(LED_DATA_PORT, LED_DATA_PIN, GPIO_PIN_SET);
        else
            HAL_GPIO_WritePin(LED_DATA_PORT, LED_DATA_PIN, GPIO_PIN_RESET);

        HAL_GPIO_WritePin(LED_CLK_PORT, LED_CLK_PIN, GPIO_PIN_SET);
    }

    HAL_GPIO_WritePin(LED_LATCH_PORT, LED_LATCH_PIN, GPIO_PIN_SET);
}

static void led_all_on(void)  { led_shift_out(0xFF); }
static void led_all_off(void) { led_shift_out(0x00); }

/* 0~7 범위를 벗어난 위치를 반대쪽 끝으로 되돌려주는 함수.
 * (시계 바늘이 12시를 지나면 다시 1시로 돌아가는 것과 같은 원리)
 * LED가 0~7번, 8개뿐이라서 7번 다음은 0번, 0번 이전은 7번으로
 * 이어줘야 "끊기지 않고 도는" 것처럼 보임 */
static uint8_t wrap8(int pos)
{
    if (pos < 0) pos += 8;
    if (pos > 7) pos -= 8;
    return (uint8_t)pos;
}

void LED_Bar_Depart(void)
{
    s_anim = LED_ANIM_DEPART_BLINK;
    s_blink_cnt = 0;
    s_last_tick = HAL_GetTick();
    led_all_on();
}

void LED_Bar_Arrive(void)
{
    s_anim = LED_ANIM_ARRIVE_BLINK;
    s_blink_cnt = 0;
    s_last_tick = HAL_GetTick();
    led_all_on();
}

void LED_Bar_Go_Up(void)
{
    s_anim      = LED_ANIM_UP;
    s_step      = 0;
    s_last_tick = HAL_GetTick();
}

void LED_Bar_Go_Down(void)
{
    s_anim      = LED_ANIM_DOWN;
    s_step      = 7;
    s_last_tick = HAL_GetTick();
}

void LED_Bar_Update(void)
{
    uint32_t now = HAL_GetTick();

    if (emergency_active || inspection_active || floor_skip_active || move_timeout_active) {
        if ((now / 500) % 2 == 0) led_shift_out(0xFF);
        else                      led_shift_out(0x00);
        return;
    }

    switch (s_anim)
    {
        case LED_ANIM_DEPART_BLINK:
            if (now - s_last_tick >= BLINK_INTERVAL_MS)
            {
                s_last_tick = now;
                s_blink_cnt++;
                if (s_blink_cnt < 6) {
                    if (s_blink_cnt % 2 == 1) led_all_off();
                    else led_all_on();
                } else {
                    led_all_on();
                    s_anim = LED_ANIM_IDLE;
                }
            }
            break;

        case LED_ANIM_ARRIVE_BLINK:
            if (now - s_last_tick >= BLINK_INTERVAL_MS)
            {
                s_last_tick = now;
                s_blink_cnt++;
                if (s_blink_cnt < 6) {
                    if (s_blink_cnt % 2 == 1) led_all_off();
                    else led_all_on();
                } else {
                    led_all_off();
                    s_anim = LED_ANIM_IDLE;
                }
            }
            break;

        case LED_ANIM_UP:
            /* 250ms(RUN_INTERVAL_MS)마다 한 번씩만 아래 내용을 실행.
             * HAL_Delay 없이 "시간이 됐는지"만 확인하는 방식이라
             * 그 사이사이에 모터·버튼 같은 다른 작업도 계속 돌아감 */
            if (now - s_last_tick >= RUN_INTERVAL_MS)
            {
                s_last_tick = now;

                /* 맨 앞에서 빛나는 LED 1개(head) + 뒤로 이어지는 꼬리 2개(tail1, tail2)
                 * = 총 3개를 동시에 켜서 "혜성처럼 흐르는" 잔상 효과를 만듦 */
                uint8_t head  = s_step;
                uint8_t tail1 = wrap8(head - 1);   // head 바로 한 칸 뒤
                uint8_t tail2 = wrap8(head - 2);   // 그 뒤 한 칸 더

                /* 1 << n  =  n번째 칸에만 불이 켜진 8비트 값 (예: 1<<3 = 00001000)
                 * 이 세 개를 |(OR)로 합치면 세 칸이 동시에 켜진 값이 됨 */
                led_shift_out((1 << head) | (1 << tail1) | (1 << tail2));

                s_step = wrap8(s_step + 1);   // 다음 차례엔 한 칸 위로 이동
            }
            break;

        case LED_ANIM_DOWN:
            /* 위(UP)와 완전히 똑같은 방식인데, 꼬리 방향과 이동 방향만 반대 */
            if (now - s_last_tick >= RUN_INTERVAL_MS)
            {
                s_last_tick = now;

                uint8_t head  = s_step;
                uint8_t tail1 = wrap8(head + 1);
                uint8_t tail2 = wrap8(head + 2);

                led_shift_out((1 << head) | (1 << tail1) | (1 << tail2));

                s_step = wrap8(s_step - 1);   // 다음 차례엔 한 칸 아래로 이동
            }
            break;

        default:
            break;
    }
}

/* =========================================================
 * 2. FND (74HC595) — SER=PB5 / SRCLK=PB4 / RCLK=PB3
 *    ※ PB9/PB8/PC9 -> PB5/PB4/PB3 (I2C1을 PB8/PB9로 옮기며 재배치)
 * ========================================================= */
void FND_Shift_Data(uint8_t data)
{
    const uint8_t FND_NUM[10] =
    {
        0x3F, 0x06, 0x5B, 0x4F, 0x66,
        0x6D, 0x7D, 0x07, 0x7F, 0x6F
    };

    if (data > 9)
    {
        if (data == 10) data = 0x79;   // 'E'
        else if (data == 11) data = 0x00; // 빈 화면(공백)
        else if (data == 12) data = 0x40; // 점검 모드 '-' 표시 (가운데 가로줄, 세그먼트 g)
    }
    else
    {
        data = FND_NUM[data];
    }

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);   // 래치(RCLK) 로우

    for (int i = 0; i < 8; i++)
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);  // 시프트 클럭(SRCLK) 로우

        if (data & (0x80 >> i))
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
        else
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);

        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);    // 시프트 클럭(SRCLK) 하이
    }

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);     // 래치(RCLK) 하이
}

void FND_Scan(void)
{
    if (emergency_active || floor_skip_active || move_timeout_active)
    {
        if ((HAL_GetTick() / 500) % 2 == 0) FND_Shift_Data(10);
        else                                 FND_Shift_Data(11);
    }
    else if (inspection_active)
    {
        // 점검 모드: 500ms 주기로 가운데 가로줄('-')과 빈 화면을 번갈아 깜빡임
        if ((HAL_GetTick() / 500) % 2 == 0) FND_Shift_Data(12);
        else                                 FND_Shift_Data(11);
    }
    else
    {
        FND_Shift_Data(current_floor);
    }
}

/* =========================================================
 * 3. RGB 운행 상태 LED — PA0(R)/PA1(G)/PA4(B), 공통캐소드
 * ========================================================= */
void RGB_Init(void)
{
    RGB_SetColor(RGB_OFF);
}

void RGB_SetColor(RGB_Color_t color)
{
    GPIO_PinState r = GPIO_PIN_RESET, g = GPIO_PIN_RESET, b = GPIO_PIN_RESET;

    switch (color)
    {
        case RGB_GREEN:  g = GPIO_PIN_SET; break;
        case RGB_YELLOW: r = GPIO_PIN_SET; g = GPIO_PIN_SET; break;
        case RGB_RED:    r = GPIO_PIN_SET; break;
        case RGB_BLUE:   b = GPIO_PIN_SET; break;
        default: break; // RGB_OFF
    }

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, r);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, g);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, b);
}

/* =========================================================
 * 4. 1602 LCD — I2C1 + PCF8574 백팩, 4비트 모드
 * =========================================================
 * PCF8574 비트맵: P7 P6 P5 P4 | P3 P2 P1 P0
 *                 D7 D6 D5 D4 | BL EN RW RS
 */
#define LCD_BACKLIGHT   0x08
#define LCD_EN          0x04
#define LCD_RW          0x02
#define LCD_RS          0x01

/* LCD_Init()에서 딱 한 번 짧은 타임아웃으로 응답 여부를 확인해두고,
   응답이 없으면(배선 안 됨/모듈 미장착) 이후 I2C 쓰기를 전부 건너뛴다.
   이게 없으면 LCD가 없을 때 매 쓰기마다 최대 100ms씩 대기가 누적되어,
   IWDG 타임아웃(약 4초)을 넘겨 워치독 리셋 루프에 빠질 수 있다. */
static uint8_t s_lcd_present = 0;

static void lcd_i2c_write(uint8_t data)
{
    if (!s_lcd_present) return;
    HAL_I2C_Master_Transmit(&hi2c1, LCD_I2C_ADDR, &data, 1, 100);
}

static void lcd_pulse_enable(uint8_t data)
{
    lcd_i2c_write(data | LCD_EN);
    HAL_Delay(1);
    lcd_i2c_write(data & ~LCD_EN);
    HAL_Delay(1);
}

static void lcd_write4bits(uint8_t nibble)
{
    /* nibble은 호출자(lcd_send)가 이미 "상위 4비트 = 데이터, 0비트 = RS"로
     * 다 조립해서 넘겨줌. 그런데 여기서 & 0xF0을 하면 RS 비트(0x01)가
     * 지워져서 글자(RS=1)를 써도 매번 명령(RS=0)으로 오인식되는 버그였음
     * → LCD_Print로 글자를 보내면 전부 커맨드로 처리되어 화면이 깨졌던 원인.
     * 그래서 재마스킹 없이 백라이트 비트만 추가한다. */
    uint8_t data = nibble | LCD_BACKLIGHT;
    lcd_i2c_write(data);
    lcd_pulse_enable(data);
}

static void lcd_send(uint8_t value, uint8_t mode) // mode: 0=명령, 1=데이터
{
    uint8_t rs = mode ? LCD_RS : 0;
    lcd_write4bits((value & 0xF0)       | rs);
    lcd_write4bits(((value << 4) & 0xF0) | rs);
}

static void lcd_command(uint8_t cmd)  { lcd_send(cmd, 0); }
static void lcd_data(uint8_t data)    { lcd_send(data, 1); }

void LCD_Init(void)
{
    HAL_Delay(50);

    /* 짧은 타임아웃(10ms)으로 딱 한 번만 응답 여부 확인. 여기서 응답이
       없으면 s_lcd_present=0으로 남아 이후 모든 I2C 쓰기가 스킵된다. */
    s_lcd_present = (HAL_I2C_IsDeviceReady(&hi2c1, LCD_I2C_ADDR, 2, 10) == HAL_OK);
    if (!s_lcd_present)
    {
        printf("[LCD] not responding at 0x%02X - skipping LCD output "
               "(wiring/address? see I2C_Scan())\r\n", LCD_I2C_ADDR >> 1);
        return;
    }

    // HD44780 4비트 초기화 시퀀스
    lcd_write4bits(0x30);
    HAL_Delay(5);
    lcd_write4bits(0x30);
    HAL_Delay(1);
    lcd_write4bits(0x30);
    HAL_Delay(1);
    lcd_write4bits(0x20); // 4비트 모드 진입
    HAL_Delay(1);

    lcd_command(0x28); // 4비트, 2줄, 5x8 폰트
    lcd_command(0x0C); // 화면 켜기, 커서 끄기, 깜빡임 끄기
    lcd_command(0x06); // 입력 모드: 커서 자동 증가
    lcd_command(0x01); // 화면 지우기
    HAL_Delay(2);
}

void LCD_Clear(void)
{
    lcd_command(0x01);
    HAL_Delay(2);
}

void LCD_SetCursor(uint8_t row, uint8_t col)
{
    uint8_t row_offsets[] = {0x00, 0x40};
    if (row > 1) row = 1;
    lcd_command(0x80 | (col + row_offsets[row]));
}

void LCD_Print(const char *str)
{
    while (*str) lcd_data((uint8_t)*str++);
}

/* 16칸 고정폭으로 출력(이전 문자 잔상 방지), 변경 없으면 I2C 미전송 */
static void lcd_print_line(uint8_t row, const char *text)
{
    static char last[2][17] = { "", "" };
    char buf[17];

    snprintf(buf, sizeof(buf), "%-16.16s", text); // 16칸 왼쪽정렬 + 공백패딩

    if (strcmp(buf, last[row]) == 0) return; // 변경 없으면 스킵(논블로킹 친화)

    LCD_SetCursor(row, 0);
    LCD_Print(buf);
    strcpy(last[row], buf);
}

/* =========================================================
 * 5. 통합 상태 표시 (RGB + LCD)
 * ========================================================= */
void Display_Init(void)
{
    RGB_Init();
    LCD_Init();
}

/* 센서 계열 오류(E101/E102/E103)면 line2를 덮어쓰고 1을 반환, 아니면 0을 반환.
   IDLE/MOVING/DOOR_OPEN 세 상태 모두 이 처리가 동일해서 공용 헬퍼로 뺐다. */
static uint8_t Apply_SensorErrorLine2(char *line2, size_t size, uint16_t error_code)
{
    if (error_code == 101)
        snprintf(line2, size, "T:%dC FAN:HIGH", (int)DHT_GetTemperature());
    else if (error_code == 102)
        snprintf(line2, size, "Sensor Timeout");
    else if (error_code == 103)
        snprintf(line2, size, "Sensor Data Err");
    else
        return 0;
    return 1;
}

void Display_Update(Display_State_t state, uint16_t error_code)
{
    char line1[17];
    char line2[17];

    switch (state)
    {
        case DISP_STATE_IDLE:
            RGB_SetColor(RGB_GREEN);
            snprintf(line1, sizeof(line1), "[ FLOOR : %dF ]", current_floor);
            if (!Apply_SensorErrorLine2(line2, sizeof(line2), error_code))
                snprintf(line2, sizeof(line2), "Temp:%dC  H:%d%%",
                         (int)DHT_GetTemperature(), (int)DHT_GetHumidity());
            break;

        case DISP_STATE_MOVING:
            RGB_SetColor(RGB_BLUE);
            snprintf(line1, sizeof(line1), "[ %dF  ->  %dF ]", current_floor, target_floor);
            if (!Apply_SensorErrorLine2(line2, sizeof(line2), error_code))
                snprintf(line2, sizeof(line2), "  MOVING %s...",
                         (target_floor > current_floor) ? "UP" : "DOWN");
            break;

        case DISP_STATE_DOOR_OPEN:
            RGB_SetColor(RGB_GREEN);
            snprintf(line1, sizeof(line1), "[ FLOOR : %dF ]", current_floor);
            if (!Apply_SensorErrorLine2(line2, sizeof(line2), error_code))
                snprintf(line2, sizeof(line2), "* DOOR OPEN *");
            break;

        case DISP_STATE_ERROR:
            RGB_SetColor(((HAL_GetTick() / 500) % 2 == 0) ? RGB_RED : RGB_OFF);
            if (error_code == 301)      snprintf(line1, sizeof(line1), "!POSITION ERR!");
            else if (error_code == 201) snprintf(line1, sizeof(line1), "!MOVE TIMEOUT!");
            else                        snprintf(line1, sizeof(line1), "! EMERGENCY !");
            snprintf(line2, sizeof(line2), "System Locked");
            break;

        case DISP_STATE_INSPECTION:
            // 0.5초 주기로 RGB 점멸 (ERROR와 동일하되 색만 노랑으로 구분)
            RGB_SetColor(((HAL_GetTick() / 500) % 2 == 0) ? RGB_YELLOW : RGB_OFF);
            snprintf(line1, sizeof(line1), "! INSPECTION !");
            snprintf(line2, sizeof(line2), "System Inspect");
            break;
    }

    lcd_print_line(0, line1);
    lcd_print_line(1, line2);
}

/* =========================================================
 * 6. 디버그용 I2C 스캐너 — 0x01~0x7F 주소를 하나씩 두드려보고
 *    응답(ACK)한 주소만 UART로 출력. LCD 안 보일 때 배선/주소 확인용.
 * ========================================================= */
void I2C_Scan(void)
{
    printf("[I2C] Scanning bus...\r\n");

    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 128; addr++)
    {
        /* 해당 주소에 장치가 있으면 HAL_OK, 없으면 타임아웃/NACK으로 실패 */
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 2, 10) == HAL_OK)
        {
            printf("[I2C] Found device at 0x%02X\r\n", addr);
            found++;
        }
    }

    if (found == 0)
        printf("[I2C] No device responded. Check wiring(SCL=PB8/SDA=PB9)/power.\r\n");
    else
        printf("[I2C] Scan done. %d device(s) found.\r\n", found);
}
