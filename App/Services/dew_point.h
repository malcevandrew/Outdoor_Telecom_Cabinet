#ifndef DEW_POINT_H
#define DEW_POINT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONDENSATION_RISK_UNKNOWN = 0, // Данные датчиков недостоверны 
    CONDENSATION_RISK_NORMAL,      // Нормальный температурный запас 
    CONDENSATION_RISK_WARNING,     // Приближение к точке росы 
    CONDENSATION_RISK_CRITICAL     // Критический риск конденсации 
} condensation_risk_t;

typedef struct {
    float dew_point_c;         // Рассчитанная точка росы (°C) 
    float temp_margin_c;       // Запас: T_surface - T_dew (°C) 
    condensation_risk_t risk;  // Уровень риска конденсации 
    bool is_valid;             // Флаг достоверности расчёта 
} dew_point_result_t;

dew_point_result_t DewPoint_Calculate(float air_temp_c, bool air_valid,
                                      float humidity_rh,
                                      float surface_temp_c, bool surface_valid);

#ifdef __cplusplus
}
#endif

#endif /* DEW_POINT_H */