#ifndef CLIMATE_CTRL_H
#define CLIMATE_CTRL_H

#include "dew_point.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t fan_pwm_duty;       // Скважность ШИМ кулера (0..1000, т.е. 0..100.0%) 
    bool fan_active;             // Флаг работы кулера 
    bool heater_relay_on;        // Состояние реле нагревателя 
    bool heat_req_low_temp;      // Флаг необходимости нагрева из-за низкой температуры воздуха 
    bool heat_req_condensation;  // Флаг необходимости нагрева из-за риска конденсации 
    bool overheat_lockout;       // Блокировка нагревателя при перегреве 
} climate_status_t;

void ClimateCtrl_Init(void);
void ClimateCtrl_Update(float air_temp_c, bool air_valid, float fan_setpoint_c, const dew_point_result_t *dew_res);
climate_status_t ClimateCtrl_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* CLIMATE_CTRL_H */