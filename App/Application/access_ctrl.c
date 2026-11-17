#include "access_ctrl.h"
#include "gpio.h"
#include <string.h>

#define DOOR_GPIO_PORT          GPIOB           // Пин куда подключен геркон, реагирующий на открытие двери
#define DOOR_GPIO_PIN           GPIO_PIN_1

// Для контроля окрытия дверцы используется геркон (нормально-замкнутый) с подтяжкой Pull-Down
// То есть если на пине HIGH (3.3 В) - дверца закрыта,  LOW (0 В) - дверца открыта или обрыв провода 
#define DOOR_OPEN_PIN_STATE     GPIO_PIN_RESET  

#define DEBOUNCE_DELAY_MS       50U             // Задержка для программного антидребезга
#define AUTH_TIMEOUT_MS         15000U          // 15 секунд на предъявление карты 

// UID разрешенной карты
static const uint8_t ALLOWED_UIDS[][RC522_UID_MAX_LEN] = {
    {0xE0, 0xBA, 0xC7, 0xA4},
    {0xE6, 0xEC, 0x6D, 0xAC}
};

// Общее количество разрешенных карт
#define ALLOWED_UIDS_COUNT (sizeof(ALLOWED_UIDS) / sizeof(ALLOWED_UIDS[0]))

static volatile bool exti_pending = false;
static volatile uint32_t exti_timestamp_ms = 0;

static access_status_t status = {
    .state = ACCESS_STATE_CLOSED,
    .door_open = false,
    .last_card_rejected = false
};

static uint32_t auth_start_ms = 0;

// Ставим флаг об открытия двери и отсекаем время
// Вызывается в случае аппаратного прерывания из HAL_GPIO_EXTI_Callback()
void AccessCtrl_OnDoorExti(void)
{
    exti_pending = true;
    exti_timestamp_ms = HAL_GetTick();
}

// Проверяем входит ли UID приложеной карты в список разрешенных
static bool IsUidAllowed(const uint8_t uid[RC522_UID_MAX_LEN])
{
    for (uint32_t i = 0; i < ALLOWED_UIDS_COUNT; i++) {
        if (memcmp(uid, ALLOWED_UIDS[i], RC522_UID_MAX_LEN) == 0) {
            return true;
        }
    }
    return false;
}

// Инициализация
void AccessCtrl_Init(void)
{
    GPIO_PinState pin = HAL_GPIO_ReadPin(DOOR_GPIO_PORT, DOOR_GPIO_PIN);
    status.door_open = (pin == DOOR_OPEN_PIN_STATE);
    status.state = status.door_open ? ACCESS_STATE_WAIT_AUTH : ACCESS_STATE_CLOSED;

    if (status.door_open) {
        auth_start_ms = HAL_GetTick();
    }
}

// Запуск процедуры идентификации при открытии дверцы шкафа
void AccessCtrl_Update(rc522_t *rfid)
{
    uint32_t now = HAL_GetTick();

    // Если сработало прерывание
    if (exti_pending && ((now - exti_timestamp_ms) >= DEBOUNCE_DELAY_MS)) {     // Используем программный антидребезг
        exti_pending = false;

        GPIO_PinState pin = HAL_GPIO_ReadPin(DOOR_GPIO_PORT, DOOR_GPIO_PIN);    // Определяем текущее состоянию на пине,
        bool new_door_open = (pin == DOOR_OPEN_PIN_STATE);                      // чтобы сравнить с прошлым состоянием

        if (new_door_open != status.door_open) {            // И если состояние на пине изменилось, значит дверцу открыли или закрыли
            status.door_open = new_door_open;              
            status.last_card_rejected = false;

            if (status.door_open) {                         // Если дверцу открыли,           
                status.state = ACCESS_STATE_WAIT_AUTH;      // то переводим в состояние ожидания карты
                auth_start_ms = now;
            } else {                                        // Если дверцу закрыли,                                                                          
                status.state = ACCESS_STATE_CLOSED;         // то переводим в штатный режим 
            }
        }
    }

    // При открытой дверце - ожидаем карту
    if (status.state == ACCESS_STATE_WAIT_AUTH) {           
        uint32_t elapsed = now - auth_start_ms;

        if (elapsed >= AUTH_TIMEOUT_MS) {                   // Если время ожидания карты вышло,                    
            status.state = ACCESS_STATE_ALARM;              // то переходим в режим тревоги
        }
    }

    // Опрос RFID-считывателя, если дверца открыта 
    if (status.door_open && rfid != NULL && rfid->is_valid) {   // Если дверцы сейчас открыта,  
        uint8_t uid[RC522_UID_MAX_LEN] = {0};

        if (RC522_PollCard(rfid, uid)) {                     // а RFID-считыватель обнаружил карту и считал её UID
            if (IsUidAllowed(uid)) {                         // Если карта входит в список разрешенных,
                status.state = ACCESS_STATE_AUTHORIZED;      // Переводим в состояние успешной авторизации
                status.last_card_rejected = false;
            } else {                                         // Если UID карты не входит в перечень разрешенных,
                status.state = ACCESS_STATE_ALARM;           // то переходим в режим тревоги
                status.last_card_rejected = true;
            }
        }
    }
}

const access_status_t *AccessCtrl_GetStatus(void)
{
    return &status;
}