#include "logger.h"

#if ENABLE_UART_LOGS
#include "usart.h"

// Перенаправление стандартного вывода printf в USART1
int _write(int file, char *ptr, int len)
{
    (void)file; // Не выводить предупреждение "unused variable"
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, (uint16_t)len, 100);
    return len;
}

void Logger_Init(void)
{
    printf("\r\n=== Telecom Cabinet Controller v1.0 ===\r\n");
    printf("[SYS] System initialized. Clock: %lu Hz\r\n", HAL_RCC_GetHCLKFreq());
}
#endif