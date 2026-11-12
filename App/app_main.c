#include "app_main.h"
#include "main.h"
#include "usart.h"
#include "analog_input.h"
#include <stdio.h>


// Перенаправление стандартного вывода printf в USART1
int _write(int file, char *ptr, int len)
{
    (void)file; // Не выводить unused variable
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, (uint16_t)len, 100);
    return len;
}

static uint32_t last_heartbeat_ms = 0;
static uint32_t last_adc_proc_ms  = 0;
static uint32_t last_print_ms     = 0;

void App_Init(void)
{
    printf("\r\n=== Telecom Cabinet Controller v1.0 ===\r\n");
    printf("[SYS] System initialized. Clock: %lu Hz\r\n", HAL_RCC_GetHCLKFreq());

    AnalogInput_Init();
    printf("[ADC] Circular DMA scan started.\r\n");
}

void App_Loop(void)
{
    uint32_t now = HAL_GetTick();

    // Фильтрация и расчёт аналоговых данных (раз в 50 мс)
    if ((now - last_adc_proc_ms) >= 50U) {
        last_adc_proc_ms = now;
        AnalogInput_Process();
    }


    // Heartbeat на светодиоде PC13 (раз в 500 мс)
    if ((now - last_heartbeat_ms) >= 500U) {
        last_heartbeat_ms = now;
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }


    // Вывод диагностики в UART (раз в 1000 мс)
    if ((now - last_print_ms) >= 1000U) {
        last_print_ms = now;

        analog_data_t adc = AnalogInput_GetData();
        
        if (adc.is_valid) {
            printf("[ADC] Surface: %.2f C (%.3f V, raw=%u) | Fan Setpoint: %.1f C (raw=%u)\r\n",
                   adc.surface_temp_c, adc.surface_voltage_v, adc.raw_temp_ch,
                   adc.fan_setpoint_c, adc.raw_setpoint_ch);
        } else {
            printf("[ADC] ERROR: Surface sensor is broken (raw=%u, %.3f V)\r\n",
                   adc.raw_temp_ch, adc.surface_voltage_v);
        }
    }
}