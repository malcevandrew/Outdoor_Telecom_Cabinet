#ifndef APP_MAIN_H
#define APP_MAIN_H

#include "aht20.h"
#include "bmp280.h"
#include "ina219.h"
#include "analog_input.h"
#include "dew_point.h"
#include "climate_ctrl.h"
#include "access_ctrl.h"
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const aht20_t *aht20;
    const bmp280_t *bmp280;
    const ina219_t *ina219;
    const analog_data_t *analog;
    const dew_point_result_t *dew;
    const climate_status_t *climate;
    const access_status_t *access;

    //Флаги внештатных ситуаций
    bool access_alarm;
    bool dew_critical;
    bool sensor_fault;
} system_telemetry_t;

void App_Init(void);
void App_Loop(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_H */