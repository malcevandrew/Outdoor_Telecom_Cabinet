#ifndef ALARM_CTRL_H
#define ALARM_CTRL_H

#include "app_main.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ALARM_LEVEL_NONE = 0,
    ALARM_LEVEL_SENSOR_FAULT,  // Отказ одного из датчиков
    ALARM_LEVEL_CONDENSATION,  // Критический риск конденсации
    ALARM_LEVEL_ACCESS_BREACH  // Наивысший приоритет: взлом / неавторизованный доступ
} alarm_level_t;

void AlarmCtrl_Init(void);
void AlarmCtrl_Update(const system_telemetry_t *sys);
alarm_level_t AlarmCtrl_GetActiveLevel(void);

#ifdef __cplusplus
}
#endif

#endif /* ALARM_CTRL_H */