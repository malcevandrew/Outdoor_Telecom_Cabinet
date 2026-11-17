#include "alarm_ctrl.h"
#include "gpio.h"

// Пин сигнала для сирены
#define BUZZER_GPIO_PORT    GPIOB
#define BUZZER_GPIO_PIN     GPIO_PIN_12

static alarm_level_t active_level = ALARM_LEVEL_NONE;

// Подача сигнала на пин сирены
static void SetBuzzerPin(bool on)
{
    HAL_GPIO_WritePin(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

// Инициализация
void AlarmCtrl_Init(void)
{
    active_level = ALARM_LEVEL_NONE;
    SetBuzzerPin(false);
}

// Обновление шаблона сирены о внештатной ситуации
void AlarmCtrl_Update(const system_telemetry_t *sys)
{
    if (sys == NULL) {
        return;
    }

    uint32_t now = HAL_GetTick();

    if (sys->access_alarm) {                                 // Если состояние тревоги из-за открытой двери, то быстрая прерывистая сирена
        active_level = ALARM_LEVEL_ACCESS_BREACH;
        SetBuzzerPin(((now / 200U) % 2U) == 0U);
    } else if (sys->dew_critical) {                          // Если близка точка росы, то двойной импульс раз в 1.5 сек
        active_level = ALARM_LEVEL_CONDENSATION;
        uint32_t phase = now % 1500U;
        SetBuzzerPin((phase < 150U) || (phase >= 300U && phase < 450U));
    } else if (sys->sensor_fault) {                          // Если один из датчиков вышел из строя, то короткий одиночный сигнал раз в 2 секунды
        active_level = ALARM_LEVEL_SENSOR_FAULT;
        SetBuzzerPin((now % 2000U) < 100U);
    } else {
        active_level = ALARM_LEVEL_NONE;
        SetBuzzerPin(false);
    }
}

alarm_level_t AlarmCtrl_GetActiveLevel(void)
{
    return active_level;
}