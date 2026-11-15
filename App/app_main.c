#include "app_main.h"
#include "main.h"
#include "usart.h"
#include "i2c.h"
#include "analog_input.h"
#include "aht20.h"
#include "bmp280.h"
#include "ina219.h"
#include "lcd1602.h"
#include <stdio.h>


// Перенаправление стандартного вывода printf в USART1
int _write(int file, char *ptr, int len)
{
    (void)file; // Не выводить unused variable
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, (uint16_t)len, 100);
    return len;
}


static aht20_t aht20;
static bmp280_t bmp280;
static ina219_t ina219;
static lcd1602_t lcd;


void App_Init(void)
{
    printf("\r\n=== Telecom Cabinet Controller v1.0 ===\r\n");
    printf("[SYS] System initialized. Clock: %lu Hz\r\n", HAL_RCC_GetHCLKFreq());

    AHT20_Init(&aht20, &hi2c1);

    if (BMP280_Init(&bmp280, &hi2c1)) {
        printf("[BMP280] Initialized successfully.\r\n");
    } else {
        printf("[BMP280] ERROR: Init failed!\r\n");
    }

    INA219_Init(&ina219, &hi2c1);

    AnalogInput_Init();
    printf("[ADC] Circular DMA scan started.\r\n");

    LCD1602_Init(&lcd, &hi2c1);
    LCD1602_SetLine(&lcd, 0, "OTC ver. 0.1");
    LCD1602_SetLine(&lcd, 1, " Initialization ");
}


static uint32_t last_heartbeat_ms = 0;
static uint32_t last_adc_proc_ms  = 0;
static uint32_t last_print_ms     = 0;
static uint32_t last_aht20_proc_ms  = 0;
static uint32_t last_bmp280_proc_ms  = 0;
static uint32_t last_ina219_proc_ms  = 0;
static uint32_t last_lcd_proc_ms  = 0;


void App_Loop(void)
{
    uint32_t now = HAL_GetTick();

    // Фильтрация и расчёт аналоговых данных (раз в 50 мс)
    if ((now - last_adc_proc_ms) >= 50U) {
        last_adc_proc_ms = now;
        AnalogInput_Process();
    }

    // Измерение температуры на AHT20 (раз в 200 мс)
    if ((now - last_aht20_proc_ms) >= 200U) {
        last_aht20_proc_ms = now;
        AHT20_Process(&aht20);
    }

    // Heartbeat на светодиоде PC13 (раз в 500 мс)
    if ((now - last_heartbeat_ms) >= 500U) {
        last_heartbeat_ms = now;
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }

    // Измерение атмосферного давления на BMP280 (раз в 500 мс)
    if ((now - last_bmp280_proc_ms) >= 500U) {
        last_bmp280_proc_ms = now;
        BMP280_Process(&bmp280);
    }

    // Измерение тока и мощности нагрузки на INA219 (раз в 200 мс)
    if ((now - last_ina219_proc_ms) >= 200U) {
        last_ina219_proc_ms = now;
        INA219_Process(&ina219);
    }

    // Обновление дисплея (раз в 10 мс)
    if ((now - last_lcd_proc_ms) >= 10U) {
        last_lcd_proc_ms = now;
        LCD1602_Process(&lcd);;
    }


    // Вывод диагностики в UART (раз в 2000 мс)
    if ((now - last_print_ms) >= 2000U) {
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

        if (aht20.is_valid) {
            printf("[AHT20] Air Temp: %.2f C | Humidity: %.1f %%\r\n",
            aht20.temperature_c, aht20.humidity_rh);
        } else {
            printf("[AHT20] ERROR: Sensor offline!\r\n");
        }


        if (bmp280.is_valid) {
            printf("[BMP280] Pressure: %.1f hPa (%.1f mmHg)\r\n",
            bmp280.pressure_hpa, bmp280.pressure_mmhg);
        } else {
            printf("[BMP280] ERROR: Sensor offline!\r\n");
        }

        if (ina219.is_valid) {
            printf("[INA219] Load: %.2f V | %.1f mA | %.1f mW\r\n",
           ina219.bus_voltage_v, ina219.current_ma, ina219.power_mw);
        } else {
            printf("[INA219] ERROR: Sensor offline!\r\n");
        }       

    }
}