#include "app_main.h"
#include "main.h"
#include "usart.h"
#include "i2c.h"
#include "spi.h"

#include "aht20.h"
#include "bmp280.h"
#include "ina219.h"
#include "lcd1602.h"
#include "rc522.h"

#include "ui_ctrl.h"
#include "logger.h"
#include "alarm_ctrl.h"

#include <stdio.h>

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_1) {
        AccessCtrl_OnDoorExti();
    }
}

static system_telemetry_t sys = {0};        // Общая телеметрия
static dew_point_result_t sys_dew = {0};    // Результат расчета точки росы

// Внешние устройств
static aht20_t aht20;
static bmp280_t bmp280;
static ina219_t ina219;
static lcd1602_t lcd;
static rc522_t rc522;

void App_Init(void)
{
    Logger_Init();

    AnalogInput_Init();
    ClimateCtrl_Init();
    AccessCtrl_Init();
    UICtrl_Init();    
    AlarmCtrl_Init();

    // Привязываем указатели к структуре общей телеметрии sys
    sys.aht20   = &aht20;
    sys.bmp280  = &bmp280;
    sys.ina219  = &ina219;
    sys.analog  = AnalogInput_GetData();
    sys.dew     = &sys_dew;
    sys.climate = ClimateCtrl_GetStatus();
    sys.access  = AccessCtrl_GetStatus();

    // Инициализация внешних устройств
    bool aht_ok = AHT20_Init(&aht20, &hi2c1);
    bool bmp_ok = BMP280_Init(&bmp280, &hi2c1);
    bool ina_ok = INA219_Init(&ina219, &hi2c1);
    bool lcd_ok = LCD1602_Init(&lcd, &hi2c1);
    bool rfd_ok = RC522_Init(&rc522, &hspi1, GPIOA, GPIO_PIN_4, GPIOB, GPIO_PIN_0);

    LOG_INFO("Init status - AHT20:%d BMP280:%d INA219:%d LCD:%d RC522:%d", aht_ok, bmp_ok, ina_ok, lcd_ok, rfd_ok);
}


void App_Loop(void)
{
    static uint32_t t_50ms  = 0;
    static uint32_t t_200ms = 0;
    static uint32_t t_500ms = 0;
    static uint32_t t_2000ms = 0;

    uint32_t now = HAL_GetTick();

    // Каждый такт: если содержимое дисплея нужно изменить, то обновляем по 1 символу на каждое выполнение функции
    LCD1602_Process(&lcd);

    // Каждые 50 мс: АЦП, контроль доступа, контроль климата, UI и расчет флагов аварий 
    if ((now - t_50ms) >= 50U) {
        t_50ms = now;

        // Определение текущих значений АЦП
        AnalogInput_Process();

        // Проверка карты при открытии дверцы шкафа
        AccessCtrl_Update(&rc522);

        // Расчет точки росы на основе измерений датчиков
        sys_dew = DewPoint_Calculate(aht20.temperature_c, aht20.is_valid, aht20.humidity_rh,
                                     sys.analog->surface_temp_c, sys.analog->is_valid);

        // Контроль климата. При необходимости включение кулера или нагревателя
        ClimateCtrl_Update(aht20.temperature_c, aht20.is_valid, sys.analog->fan_setpoint_c, &sys_dew);
        
        // Обновление динамических флагов о наличии внештатной ситуации        
        sys.access_alarm = (sys.access->state == ACCESS_STATE_ALARM);                        // Дверца открыта без последующей идентификации
        sys.dew_critical = (sys_dew.is_valid && sys_dew.risk == CONDENSATION_RISK_CRITICAL); // Критически близка точка росы        
        sys.sensor_fault = (!aht20.is_valid || !bmp280.is_valid ||  !ina219.is_valid ||      // Один из датчиков вышел из строя
                                          !sys.analog->is_valid || !rc522.is_valid);

        // Формирование текста для экранов дисплея
        UICtrl_Update(&lcd, &sys);

        // Включение сирены при наличии внештатных ситуаций
        AlarmCtrl_Update(&sys);
    }

    // Каждые 200 мс: опрос быстрых I2C-датчиков
    if ((now - t_200ms) >= 200U) {
        t_200ms = now;

        AHT20_Process(&aht20);      // Датчик температуры
        INA219_Process(&ina219);    // Датчик напряжения, потребляемого тока и мощность нагрузки (оборудования в шкафу)
    }

    // Каждые 500 мс: барометр и Heartbeat
    if ((now - t_500ms) >= 500U) {
        t_500ms = now;

        BMP280_Process(&bmp280);    
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); 
    }

    // Каждые 2000 мс: вывод диагностики в UART
    if ((now - t_2000ms) >= 2000U) {
        t_2000ms = now;
        LOG_INFO("T_air=%.1fC RH=%.1f%% T_dew=%.1fC T_surf=%.1fC PWM=%u",
                 aht20.temperature_c, aht20.humidity_rh,
                 sys_dew.dew_point_c, sys.analog->surface_temp_c, sys.climate->fan_pwm_duty);
    }
}