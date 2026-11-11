#include "app_main.h"
#include "main.h"
#include "usart.h"
#include <stdio.h>

// Перенаправление стандартного вывода printf в USART1
int _write(int file, char *ptr, int len)
{
    (void)file; // Не выводить unused variable
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, (uint16_t)len, 100);
    return len;
}

static uint32_t last_heartbeat_ms = 0;

void App_Init(void)
{
    printf("\r\n=== Telecom Cabinet Controller v1.0 ===\r\n");
    printf("[SYS] System initialized. Clock: %lu Hz\r\n", HAL_RCC_GetHCLKFreq());
}

void App_Loop(void)
{
    uint32_t now = HAL_GetTick();

    // Heartbeat на светодиоде PC13 (раз в 500 мс)
    if ((now - last_heartbeat_ms) >= 500U) {
        last_heartbeat_ms = now;
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
}