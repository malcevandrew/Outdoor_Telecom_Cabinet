#include "alarm_ctrl.h"
#include "tim.h"

static alarm_level_t active_level = ALARM_LEVEL_NONE;
static uint16_t current_tone = 0U;

// Timer Clocks = 100 МГц, Prescaler (PSC) = 99 (получили частоту таймера = 1 000 000 Гц)
// Включение звука заданной частоты (тональности) со скважностью 50% 
static void SetSpeakerTone(uint16_t freq_hz)
{
    if (freq_hz == current_tone) {
        return;
    }

    current_tone = freq_hz;
    
    
    if (freq_hz == 0U) {
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0U);
        return;
    }

    uint32_t period = (1000000UL / freq_hz) - 1UL;      

    __HAL_TIM_SET_AUTORELOAD(&htim1, period);                    // Период таймера (высота, тон звука)
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, period / 2U);   // Скважность всегда 50% - максимальная громкость динамика
}

// Инициализация
void AlarmCtrl_Init(void)
{
    active_level = ALARM_LEVEL_NONE;
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    SetSpeakerTone(0U);
}

// Обновление шаблона сирены о внештатной ситуации
void AlarmCtrl_Update(const system_telemetry_t *sys)
{
    if (sys == NULL) {
        return;
    }

    uint32_t now = HAL_GetTick();

    if (sys->access_alarm) {                                // Если состояние тревоги из-за открытой двери, 
        active_level = ALARM_LEVEL_ACCESS_BREACH;           // то сирена (переключение 2500 Гц / 1800 Гц каждые 200 мс)

        uint16_t tone = (((now / 200U) % 2U) == 0U) ? 2500U : 1800U;
        SetSpeakerTone(tone);

    } else if (sys->dew_critical) {                         // Если близка точка росы, то двойной импульс 1500 Гц
        active_level = ALARM_LEVEL_CONDENSATION;    

        uint32_t phase = now % 1500U;
        bool beep = (phase < 150U) || (phase >= 300U && phase < 450U);
        SetSpeakerTone(beep ? 1500U : 0U);

    } else if (sys->sensor_fault) {                         // Если один из датчиков вышел из строя,  
        active_level = ALARM_LEVEL_SENSOR_FAULT;            // то короткий низкий сигнал 800 Гц раз в 2 секунды

        bool beep = ((now % 2000U) < 100U);
        SetSpeakerTone(beep ? 800U : 0U);

    } else {
        active_level = ALARM_LEVEL_NONE;
        SetSpeakerTone(0U);
    }
}

alarm_level_t AlarmCtrl_GetActiveLevel(void)
{
    return active_level;
}