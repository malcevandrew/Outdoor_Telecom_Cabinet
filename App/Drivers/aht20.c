#include "aht20.h"

#define AHT20_I2C_ADDR          (0x38U << 1)    // 7 битный адрес датчика(сразу смещен влево на 1 бит, для добавления бита чтения/записи функцией HAL)
#define AHT20_CMD_INIT          0xBEU           // Команда инициализации
#define AHT20_CMD_TRIGGER       0xACU           // Команда запуска процесса измерения
#define AHT20_CMD_SOFT_RESET    0xBAU           // команда для мягкого сброса (позволяет перезапустьть датчик без выключения-включения)
#define AHT20_STATUS_BUSY_MASK  0x80U           // Маска (0b10000000) для проверки бита состояния  (бит №7 в байте статуса) (1 занят/ 0 свободен)  
#define AHT20_STATUS_CAL_MASK   0x08U           // Маска (0b00001000) для проверки бита калибровки (бит №3 в байте статуса) (1 откалибр./ 0 неоткалибр.)  

#define AHT20_MEAS_TIME_MS      85U             // Время ожидания выполнения измерения датчиком
#define AHT20_I2C_TIMEOUT_MS    20U             // Тайматут для функции отправки и приема
#define AHT20_MAX_ERRORS        3U              // Количество неудачных попыток связи до перехода в состояние ошибки

// Обработчик неудачных попыток связи. Если превышается установленный лимит попыток, то переводим в состояние ошибки
static void AHT20_HandleError(aht20_t *dev)
{
    dev->error_count++;
    if (dev->error_count >= AHT20_MAX_ERRORS) {
        dev->is_valid = false;
        dev->state = AHT20_STATE_ERROR;
    } else {
        dev->state = AHT20_STATE_IDLE;
    }
}

// Инициализация датчика
bool AHT20_Init(aht20_t *dev, I2C_HandleTypeDef *hi2c)
{
    if (dev == NULL || hi2c == NULL) {
        return false;
    }

    dev->hi2c = hi2c;
    dev->dev_addr = AHT20_I2C_ADDR;
    dev->state = AHT20_STATE_IDLE;
    dev->is_valid = false;
    dev->error_count = 0;

    if (HAL_I2C_IsDeviceReady(dev->hi2c, dev->dev_addr, 3, AHT20_I2C_TIMEOUT_MS) != HAL_OK) {
        dev->state = AHT20_STATE_ERROR;
        return false;
    }

    // Проверка статуса калибровки. (Отправляем адрес датчика + бит чтения. В ответ принимаем байт статуса)
    uint8_t status = 0;
    if (HAL_I2C_Master_Receive(dev->hi2c, dev->dev_addr, &status, 1, AHT20_I2C_TIMEOUT_MS) != HAL_OK) {    
        dev->state = AHT20_STATE_ERROR;
        return false;
    }

    // Если датчик не откалиброван (бит калибровки в байте статуса = 0), то отправляем команду инициализации
    if ((status & AHT20_STATUS_CAL_MASK) == 0U) {
        uint8_t init_cmd[3] = {AHT20_CMD_INIT, 0x08U, 0x00U};   // Команда инициализации + 2 байта с параметрами по даташиту

        if (HAL_I2C_Master_Transmit(dev->hi2c, dev->dev_addr, init_cmd, 3, AHT20_I2C_TIMEOUT_MS) != HAL_OK) {
            dev->state = AHT20_STATE_ERROR;
            return false;
        }
        HAL_Delay(15);
    }

    return true;
}

// Функция контроля процесса измерения
void AHT20_Process(aht20_t *dev)
{
    if (dev == NULL || dev->hi2c == NULL) {
        return;
    }

    uint32_t now = HAL_GetTick();

    switch (dev->state) {
        case AHT20_STATE_IDLE: {
            uint8_t trig_cmd[3] = {AHT20_CMD_TRIGGER, 0x33U, 0x00U};    // Команда запуска процесса измерения + 2 байта с параметрами по даташиту

            if (HAL_I2C_Master_Transmit(dev->hi2c, dev->dev_addr, trig_cmd, 3, AHT20_I2C_TIMEOUT_MS) == HAL_OK) {
                dev->meas_start_ms = now;
                dev->state = AHT20_STATE_MEASURING;
            } else {
                AHT20_HandleError(dev);
            }
            break;
        }

        case AHT20_STATE_MEASURING: {
            // Ждём указанное по даташиту время проведения измерения
            if ((now - dev->meas_start_ms) < AHT20_MEAS_TIME_MS) {
                break;
            }

            // Если время отведенное на измерение прошло, отправляем адрес + бит чтения, получаем 6 байт (1 байт статуса + 5 байт с данными)
            uint8_t raw_data[6] = {0};
            if (HAL_I2C_Master_Receive(dev->hi2c, dev->dev_addr, raw_data, 6, AHT20_I2C_TIMEOUT_MS) != HAL_OK) {    
                AHT20_HandleError(dev);
                break;
            }

            // Если датчик всё ещё занят (бит №7 в байте статуса == 1), то ждём еще
            if ((raw_data[0] & AHT20_STATUS_BUSY_MASK) != 0U) {
                if ((now - dev->meas_start_ms) > (AHT20_MEAS_TIME_MS + 50U)) {
                    AHT20_HandleError(dev);
                }
                break;
            }

            // Сборка результатов измерения из принятых байтов с данными. 
            // Значение температуры это первые 20 бит, а значение влажности вторые 20 бит из этих пяти байт с данными
            uint32_t raw_hum = ((uint32_t)raw_data[1] << 12) |
                               ((uint32_t)raw_data[2] << 4)  |
                               ((uint32_t)raw_data[3] >> 4);

            uint32_t raw_temp = (((uint32_t)raw_data[3] & 0x0FU) << 16) |
                                ((uint32_t)raw_data[4] << 8)  |
                                (uint32_t)raw_data[5];

            // Выполняем перевод по формуле из даташита (2^20 == 1048576.0f)
            dev->humidity_rh   = ((float)raw_hum / 1048576.0f) * 100.0f;                 
            dev->temperature_c = (((float)raw_temp / 1048576.0f) * 200.0f) - 50.0f;

            dev->error_count = 0;
            dev->is_valid = true;
            dev->state = AHT20_STATE_IDLE;
            break;
        }

        case AHT20_STATE_ERROR: {
            // Попытка автоматического восстановления связи раз в 1 секунду
            if ((now - dev->meas_start_ms) >= 1000U) {
                dev->meas_start_ms = now;
                if (AHT20_Init(dev, dev->hi2c)) {
                    dev->state = AHT20_STATE_IDLE;
                }
            }
            break;
        }
    }
}
