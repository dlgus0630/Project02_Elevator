#include "main.h"
#include "fnd.h"


/* FND 출력 데이터 시프트 함수 */
void FND_Shift_Data(uint8_t data)
{
    const uint8_t FND_NUM[10] =
    {
        0x3F, 0x06, 0x5B, 0x4F, 0x66,
        0x6D, 0x7D, 0x07, 0x7F, 0x6F
    };

    if (data > 9)
    {
        if (data == 10)
            data = 0x79;      // 'E'
        else if (data == 11)
            data = 0x00;      // Blank
    }
    else
    {
        data = FND_NUM[data];
    }

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_RESET);   // Latch LOW

    for (int i = 0; i < 8; i++)
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);   // Clock LOW

        if (data & (0x80 >> i))
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
        else
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);

        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);     // Clock HIGH
    }

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_SET);         // Latch HIGH
}


/* FND 스캔 및 비상 상태 확인 함수 */
void FND_Scan(void)
{
    if (emergency_active)
    {
        // 비상 상태 → 0.5초마다 E ↔ Blank 점멸
        if ((HAL_GetTick() / 500) % 2 == 0)
            FND_Shift_Data(10);
        else
            FND_Shift_Data(11);
    }
    else
    {
        // 정상 상태 → 현재 층수 출력
        FND_Shift_Data(current_floor);
    }
}
