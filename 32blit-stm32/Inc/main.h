#pragma once
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#define HIGH(PORT, PIN) HAL_GPIO_WritePin(PORT, PIN, GPIO_PIN_SET);
#define LOW(PORT, PIN) HAL_GPIO_WritePin(PORT, PIN, GPIO_PIN_RESET);

#define DELAY(MS)       HAL_Delay(MS)
