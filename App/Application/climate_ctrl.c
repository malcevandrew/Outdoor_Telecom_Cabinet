#include "climate_ctrl.h"
#include "tim.h"
#include "gpio.h"

// Параметры управления кулером (PWM 1000 Гц)  (Timer Clocks = 100 МГц, Prescaler (PSC) = 99, Counter Period (ARR) = 999)
#define FAN_HYSTERESIS_C          1.5f      // Гистерезис отключения вентилятора  
#define FAN_PROP_BAND_C           10.0f     // Если разница температуры воздуха и порога включения кулера равна 0, то скважность ШИМ 0%.
                                            // Если эта разница больше или равна FAN_PROP_BAND_C то скважность ШИМ 100%.
                                            // А если эта разница в промежутке от 0 до FAN_PROP_BAND_C, то скважность ШИМ расчитываем пропорционально    
#define FAN_PWM_MIN_START         300U      // Минимальный ШИМ (скважность 30%), чтобы мотор уверенно стартовал  
#define FAN_PWM_MAX               1000U     // Максимальный ШИМ (скважность 100%)     

// Параметры управления нагревателем (реле на PB10)  
#define HEAT_AIR_MIN_ON_C         15.0f  // Включить нагрев, если воздух холоднее 15.0 °C  
#define HEAT_AIR_MIN_OFF_C        17.0f  // Выключить нагрев по воздуху при 17.0 °C  
#define HEAT_OVERHEAT_LIMIT_C     45.0f  // Аварийный запрет нагрева выше 45.0 °C  

// Пороги гистерезиса по конденсации (Delta T = T_surf - T_dew)  
#define COND_MARGIN_ON_C          2.0f   // Включить нагрев при запасе <= 2.0 °C  
#define COND_MARGIN_OFF_C         3.5f   // Выключить нагрев при запасе >= 3.5 °C  

// Пины исполнительных устройств (реле)
#define RELAY_GPIO_PORT           GPIOB
#define RELAY_GPIO_PIN            GPIO_PIN_10

// Реле в прототипе управляется высоким уровнем
#define RELAY_STATE_ON            GPIO_PIN_SET
#define RELAY_STATE_OFF           GPIO_PIN_RESET

static climate_status_t status = {0};

// Регулировка скважности(скорости вращения) кулера
static void SetFanPWM(uint16_t duty)
{
    if (duty > FAN_PWM_MAX) {
        duty = FAN_PWM_MAX;
    }

    status.fan_pwm_duty = duty;
    status.fan_active = (duty > 0U);                    // Если скважность 0 - кулер выключаем

    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, duty); // Изменяем регистр CCR, тем самым регулируя скважность (скорости вращения)
}

// Включение/выключение реле с нагревателем
static void SetHeaterRelay(bool enable)
{
    status.heater_relay_on = enable;
    HAL_GPIO_WritePin(RELAY_GPIO_PORT, RELAY_GPIO_PIN, enable ? RELAY_STATE_ON : RELAY_STATE_OFF);
}

// Инициализация
void ClimateCtrl_Init(void)
{
    SetHeaterRelay(false);                                 
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);   // Запуск ШИМ на третьем канале таймера №2
    SetFanPWM(0);
}

// Управляем кулером и реле нагревателя
void ClimateCtrl_Update(float air_temp_c,                   // Температура воздуха с датчика 
                        bool air_valid,                     // Исправность датчика температуры
                        float fan_setpoint_c,               // Температура при превышении которой включаем кулер
                        const dew_point_result_t *dew_res)  // Структура с данными о точке росы
{
    // При отказе датчика воздуха выключаем всё климатическое оборудование
    if (!air_valid) {
        status.heat_req_low_temp = false;
        status.heat_req_condensation = false;
        status.overheat_lockout = false;
        SetFanPWM(0);
        SetHeaterRelay(false);
        return;
    }

    // 1. Логика управления кулером 
    if (!status.fan_active) {
        if (air_temp_c >= fan_setpoint_c) {                         // Если температура воздуха выше установленного порога, то включаем кулер
            status.fan_active = true;
        }
    } else {
        if (air_temp_c <= (fan_setpoint_c - FAN_HYSTERESIS_C)) {    // Если кулер уже включен, но температура внутри меньше/равна порогу 
            status.fan_active = false;                              // с учетом гистерезиса, ты выключаем 
        }
    }

    // Регулируем скорость кулера
    if (status.fan_active) {
        float excess = air_temp_c - (fan_setpoint_c - FAN_HYSTERESIS_C);    
        float ratio = excess / FAN_PROP_BAND_C;                             // Определяем коэф. пропорции. Т.е. чем больше разница
        if (ratio < 0.0f) ratio = 0.0f;                                     // между текущей температурой и порогом, тем выше скорость кулера
        if (ratio > 1.0f) ratio = 1.0f;                                    

        // Расчитываем скважность(скорость вращения кулера)
        uint16_t pwm = FAN_PWM_MIN_START + (uint16_t)(ratio * (float)(FAN_PWM_MAX - FAN_PWM_MIN_START));   
        SetFanPWM(pwm);
    } else {
        SetFanPWM(0);
    }

    // 2. Логика управления нагревателем 
    // А. Поддержание минимальной температуры воздуха  
    if (!status.heat_req_low_temp) {
        if (air_temp_c <= HEAT_AIR_MIN_ON_C) {      // Если температура воздуха ниже порога, то требуется нагрев 
            status.heat_req_low_temp = true;
        }
    } else {
        if (air_temp_c >= HEAT_AIR_MIN_OFF_C) {     // Если температура воздуха выше порога, то нагрев не требуется
            status.heat_req_low_temp = false;
        }
    }

    // В. Защита от конденсации  
    if (dew_res != NULL && dew_res->is_valid) {
        if (!status.heat_req_condensation) {
            if (dew_res->temp_margin_c <= COND_MARGIN_ON_C) {   // Если разница между температурой воздуха и температурой точки росы  
                status.heat_req_condensation = true;            // ниже порога COND_MARGIN_ON_C (т.е. точка росы близка), то требуется нагрев 
            }
        } else {
            if (dew_res->temp_margin_c >= COND_MARGIN_OFF_C) {  // Если разница между температурой воздуха и температурой точки росы выше 
                status.heat_req_condensation = false;           // с запаса COND_MARGIN_OFF_C (т.е. точка росы еще далеко), то нагрев не требуется 
            }
        }
    } else {
        status.heat_req_condensation = false;                   // При недостоверном датчике поверхности запрос по конденсации сбрасывается
    }

    // 3. Блокировка при чрезмерно высокой температуре
    status.overheat_lockout = (air_temp_c >= HEAT_OVERHEAT_LIMIT_C);

    // Решение по включению реле нагревателя  
    bool need_heat = (status.heat_req_low_temp || status.heat_req_condensation) && !status.overheat_lockout;
    SetHeaterRelay(need_heat);
}

climate_status_t ClimateCtrl_GetStatus(void)
{
    return status;
}