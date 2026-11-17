#ifndef ACCESS_CTRL_H
#define ACCESS_CTRL_H

#include "rc522.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ACCESS_STATE_CLOSED = 0,       // Дверца закрыта, штатный режим
    ACCESS_STATE_WAIT_AUTH,        // Дверца открыта, ожидание карты (идет таймер)
    ACCESS_STATE_AUTHORIZED,       // Авторизация успешна, обслуживание разрешено
    ACCESS_STATE_ALARM             // Тревога: таймаут или неизвестная карта
} access_state_t;

typedef struct {
    access_state_t state;
    bool door_open;
    bool last_card_rejected;
} access_status_t;

void AccessCtrl_Init(void);
void AccessCtrl_OnDoorExti(void);
void AccessCtrl_Update(rc522_t *rfid);
const access_status_t *AccessCtrl_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* ACCESS_CTRL_H */