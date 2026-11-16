#include "dew_point.h"
#include <math.h>

// Коэффициенты приближения Магнуса 
#define MAGNUS_A                 17.62f
#define MAGNUS_B                 243.12f

// Пороги температурного запаса (Delta T = T_surface - T_dew)
#define MARGIN_WARNING_THRESH_C  4.0f           // Предупреждение, если запас < 4.0 °C 
#define MARGIN_CRITICAL_THRESH_C 2.0f           // Критический риск (запрос нагрева), если запас < 2.0 °C

dew_point_result_t DewPoint_Calculate(float air_temp_c, bool air_valid,
                                      float humidity_rh,
                                      float surface_temp_c, bool surface_valid)
{
    // Инициализация
    dew_point_result_t res = {
        .dew_point_c = 0.0f,
        .temp_margin_c = 0.0f,
        .risk = CONDENSATION_RISK_UNKNOWN,
        .is_valid = false
    };

    // Если хотя бы один из датчиков неисправен, расчёт не выполняется 
    if (!air_valid || !surface_valid) {
        return res;
    }

    // Защита от некорректной влажности (logf(0) даст минус бесконечность)
    if (humidity_rh < 1.0f) {
        humidity_rh = 1.0f;
    } else if (humidity_rh > 100.0f) {
        humidity_rh = 100.0f;
    }

    // Формула Магнуса
    float gamma = logf(humidity_rh / 100.0f) + ((MAGNUS_A * air_temp_c) / (MAGNUS_B + air_temp_c));
    float t_dew = (MAGNUS_B * gamma) / (MAGNUS_A - gamma);
    float margin = surface_temp_c - t_dew;

    res.dew_point_c = t_dew;
    res.temp_margin_c = margin;
    res.is_valid = true;

    if (margin <= MARGIN_CRITICAL_THRESH_C) {
        res.risk = CONDENSATION_RISK_CRITICAL;
    } else if (margin <= MARGIN_WARNING_THRESH_C) {
        res.risk = CONDENSATION_RISK_WARNING;
    } else {
        res.risk = CONDENSATION_RISK_NORMAL;
    }

    return res;
}