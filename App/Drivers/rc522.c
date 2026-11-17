#include "rc522.h"

// Основные регистры MFRC522
#define RC522_REG_COMMAND     0x01U     // Регистр начала и остановки выполнения команды
#define RC522_REG_COM_IRQ     0x04U     // Адрес регистра флагов запросов прерываний
#define RC522_REG_ERROR       0x06U     // Адрес регистра, содержащего статус ошибки последней команды
#define RC522_REG_FIFO_DATA   0x09U     // Адрес регистра ввода и вывода данных 64-байтного буфера FIFO
#define RC522_REG_FIFO_LEVEL  0x0AU     // Адрес регистра количества байтов, хранящихся в буфере FIFO
#define RC522_REG_CONTROL     0x0CU     // Адрес регистра управляющих настроек
#define RC522_REG_BIT_FRAMING 0x0DU     // Адрес регистра настройки битовых кадров (старт передачи и кол-во бит последнего байта)
#define RC522_REG_MODE        0x11U     // Адрес регистра общих настроек режимов передачи и приема
#define RC522_REG_TX_CONTROL  0x14U     // Адрес регистра управления логикой работы выводов антенны (TX1 и TX2)
#define RC522_REG_TX_ASK      0x15U     // Адрес регистра управления настройками амплитудной модуляции (100% ASK)
#define RC522_REG_T_MODE      0x2AU     // Адрес регистра настроек внутреннего таймера + старшие 4 бита предделителя
#define RC522_REG_T_PRESCALER 0x2BU     // Адрес регистра младших 8 бит предделителя таймера
#define RC522_REG_T_RELOAD_H  0x2CU     // Адрес регистра таймера (старший байт значения перезагрузки)
#define RC522_REG_T_RELOAD_L  0x2DU     // Адрес регистра таймера (младший байт значения перезагрузки)
#define RC522_REG_VERSION     0x37U     // Адрес регистра, хранящего версию чипа MFRC522 (используем для проверки связи)

// Команды MFRC522 
#define RC522_CMD_IDLE          0x00U   // Отмена текущей выполняемой команды и переход в режим ожидания
#define RC522_CMD_TRANSCEIVE    0x0CU   // Передает данные из буфера FIFO на антенну и активирует прием после отправки
#define RC522_CMD_SOFT_RESET    0x0FU   // Команда сброса

// Команды стандарта ISO14443A
#define PICC_CMD_REQA           0x26U   // Запрос поиска карт в поле антенны (Request A)
#define PICC_CMD_ANTICOLL_CL1   0x93U   // Антиколлизия / выбор карты, каскадный уровень 1 (первые 4 байта UID)

#define RC522_SPI_TIMEOUT_MS    15U     // Таймаут на отправку по SPI
#define RC522_MAX_ERRORS        3U      // Количество попыток на потправку до перехода в ошибку

// Управление линией CS. Подача LOW для активации линии MISO на прием
static inline void RC522_CS_Select(rc522_t *dev)
{
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
}

// Управление линией CS. Возвращение к HIGH для освобождения линии MISO
static inline void RC522_CS_Deselect(rc522_t *dev)
{
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
}

// Запись значения в регистр
static bool RC522_WriteReg(rc522_t *dev, uint8_t reg, uint8_t val)
{
    // По даташиту байт адреса имеет структуру [x a a a a a a 0],
    // где x = 1, если производим чтение или x = 0, если производим запись. a a a a a a - 6-битный адрес регистра
    uint8_t tx[2] = {(uint8_t)((reg << 1) & 0x7EU), val};       
    RC522_CS_Select(dev);

    HAL_StatusTypeDef st = HAL_SPI_Transmit(dev->hspi, tx, 2, RC522_SPI_TIMEOUT_MS);

    RC522_CS_Deselect(dev);
    return (st == HAL_OK);
}

// Чтение значения из регистра
static bool RC522_ReadReg(rc522_t *dev, uint8_t reg, uint8_t *val)
{
    uint8_t addr = (uint8_t)(((reg << 1) & 0x7EU) | 0x80U);     // Первый бит устанавливаем в 1 (чтение)
    uint8_t dummy = 0x00U;
    uint8_t rx = 0x00U;

    RC522_CS_Select(dev);

    HAL_StatusTypeDef st1 = HAL_SPI_Transmit(dev->hspi, &addr, 1, RC522_SPI_TIMEOUT_MS);                // Отправка адреса
    HAL_StatusTypeDef st2 = HAL_SPI_TransmitReceive(dev->hspi, &dummy, &rx, 1, RC522_SPI_TIMEOUT_MS);   // Чтение ответа

    RC522_CS_Deselect(dev);

    if (st1 != HAL_OK || st2 != HAL_OK) {
        return false;
    }
    *val = rx;
    return true;
}

// Устанавливаем в 1 биты регистра указанные в маске
static void RC522_SetBitMask(rc522_t *dev, uint8_t reg, uint8_t mask)
{
    uint8_t tmp = 0;
    if (RC522_ReadReg(dev, reg, &tmp)) {
        RC522_WriteReg(dev, reg, tmp | mask);    // 0000 | 0101 == 0101
    }
}

// Устанавливам в 0 биты регистра указанные в маске
static void RC522_ClearBitMask(rc522_t *dev, uint8_t reg, uint8_t mask)
{
    uint8_t tmp = 0;
    if (RC522_ReadReg(dev, reg, &tmp)) {
        RC522_WriteReg(dev, reg, tmp & (uint8_t)(~mask));   // 1111 & ~(0101) == 1010
    }
}

// Инициализация
bool RC522_Init(rc522_t *dev, SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin, GPIO_TypeDef *rst_port, uint16_t rst_pin)
{
    if (dev == NULL || hspi == NULL) {
        return false;
    }

    dev->hspi = hspi;
    dev->cs_port = cs_port;
    dev->cs_pin = cs_pin;
    dev->rst_port = rst_port;
    dev->rst_pin = rst_pin;
    dev->is_valid = false;
    dev->error_count = 0;

    RC522_CS_Deselect(dev);

    // Аппаратный сброс
    HAL_GPIO_WritePin(dev->rst_port, dev->rst_pin, GPIO_PIN_RESET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(dev->rst_port, dev->rst_pin, GPIO_PIN_SET);
    HAL_Delay(10);

    // Проверка связи по SPI (выполняем чтение регистра содержащего версию чипа)
    uint8_t version = 0;
    if (!RC522_ReadReg(dev, RC522_REG_VERSION, &version)) {
        return false;
    }
    if (version == 0x00U || version == 0xFFU) {
        return false;
    }

    // Шаблон настройки внутреннего таймера и модуляции
    RC522_WriteReg(dev, RC522_REG_T_MODE, 0x8DU);
    RC522_WriteReg(dev, RC522_REG_T_PRESCALER, 0x3EU);
    RC522_WriteReg(dev, RC522_REG_T_RELOAD_L, 30U);
    RC522_WriteReg(dev, RC522_REG_T_RELOAD_H, 0U);
    RC522_WriteReg(dev, RC522_REG_TX_ASK, 0x40U);
    RC522_WriteReg(dev, RC522_REG_MODE, 0x3DU);

    // Включение антенны (устанавливаем два младших бита Tx1RFEn и Tx2RFEn регистра RC522_REG_TX_CONTROL в 1)
    uint8_t tx_ctrl = 0;
    if (RC522_ReadReg(dev, RC522_REG_TX_CONTROL, &tx_ctrl)) {
        if ((tx_ctrl & 0x03U) != 0x03U) {                           // Проверяем установлены ли Tx1RFEn и Tx2RFEn в 1, если нет устанавливаем
            RC522_SetBitMask(dev, RC522_REG_TX_CONTROL, 0x03U);
        }
    }

    dev->is_valid = true;
    return true;
}

// Выполнить одну радио-транзакцию: «отправить запрос в эфир -> поймать ответ из эфира»
static bool RC522_Transceive(rc522_t *dev, const uint8_t *tx_data, uint8_t tx_len, uint8_t *rx_data, uint8_t *rx_len, uint8_t valid_bits)
{
    RC522_WriteReg(dev, RC522_REG_COMMAND, RC522_CMD_IDLE);         // Переводим в режим ожидания(останавливаем любую текущую работу RC522)
    RC522_WriteReg(dev, RC522_REG_COM_IRQ, 0x7FU);                  // Сброс старых флагов прерывания(устанавливаем биты регистра COM_IRQ в 1), чтобы 
    RC522_SetBitMask(dev, RC522_REG_FIFO_LEVEL, 0x80U);             // Очистка внутреннего буфера FIFO (устанавливаем старший бит в 1)

    for (uint8_t i = 0; i < tx_len; i++) {                           
        RC522_WriteReg(dev, RC522_REG_FIFO_DATA, tx_data[i]);       // По одному байту передаем в регистр FIFODataReg, наполняя буфер FIFO
    }

    RC522_WriteReg(dev, RC522_REG_BIT_FRAMING, valid_bits);         // В регистр BitFramingReg (биты TxLastBits[2:0]) записываем количество бит в последнем байте (для команды поиска карты REQA по стандарту отправляется 7 бит, т.о. valid_bits = 0x07).
    RC522_WriteReg(dev, RC522_REG_COMMAND, RC522_CMD_TRANSCEIVE);   // Переводим чип в режим передача + приём
    RC522_SetBitMask(dev, RC522_REG_BIT_FRAMING, 0x80U);            // В регистр BitFramingReg устанавливаем старший бит (StartSend) в 1, там самым активируя передачу содержимого из буфера FIFO в эфир

    // По завершении передачи ожидаем ответ (до 15 мс). Опрашиваем регистр статуса COM_IRQ (в нем хранятся флаги), ожидая одно из трёх событий
    uint32_t start_ms = HAL_GetTick();
    uint8_t irq = 0;
    while (1) {
        if (!RC522_ReadReg(dev, RC522_REG_COM_IRQ, &irq)) {
            return false;
        }
        if (irq & 0x30U) {      // Проверяем биты RxIRq или IdleIRq в регистре ComIrqReg (карта ответила, данные легли в FIFO)
            break;
        }
        if (irq & 0x01U) {      // Проверяем бит TimerIRq в регистре ComIrqReg (внутренний таймер RC522 истек, а карта не ответила)
            return false;
        }
        if ((HAL_GetTick() - start_ms) > 15U) {     // Защитный таймаут
            return false;
        }
    }

    RC522_ClearBitMask(dev, RC522_REG_BIT_FRAMING, 0x80U);  // Устанавливаем бит StartSend обратно в 0

    // Если ответ пришел, проверям регистр ERROR (не было ли наложения двух карт)
    uint8_t err = 0;
    RC522_ReadReg(dev, RC522_REG_ERROR, &err);
    if (err & 0x1BU) {                              // Биты №4 (BufferOvfl), №1 (ParityErr), №0 (ProtocolErr)
        return false;
    }

    // Из регистра FIFO_LEVEL считываем, сколько байт прислала карта
    uint8_t fifo_len = 0;
    RC522_ReadReg(dev, RC522_REG_FIFO_LEVEL, &fifo_len);
    if (fifo_len > *rx_len) {
        fifo_len = *rx_len;
    }
    *rx_len = fifo_len;

    // Вычитываем эти байты из FIFO_DATA в массив rx_data
    for (uint8_t i = 0; i < fifo_len; i++) {
        RC522_ReadReg(dev, RC522_REG_FIFO_DATA, &rx_data[i]);
    }

    return true;
}

// Опрос карты
bool RC522_PollCard(rc522_t *dev, uint8_t uid_out[RC522_UID_MAX_LEN])
{
    if (dev == NULL || uid_out == NULL) {
        return false;
    }

    // Проверка связи по SPI (выполняем чтение регистра содержащего версию чипа)
    uint8_t ver = 0;
    if (!RC522_ReadReg(dev, RC522_REG_VERSION, &ver) || ver == 0x00U || ver == 0xFFU) {
        dev->error_count++;
        if (dev->error_count >= RC522_MAX_ERRORS) {
            dev->is_valid = false;
        }
        return false;
    }
    dev->error_count = 0;
    dev->is_valid = true;

    // 1. Поиск карты в поле (Request Type A - 7 бит) 
    uint8_t req_cmd = PICC_CMD_REQA;
    uint8_t atqa[2] = {0};
    uint8_t atqa_len = sizeof(atqa);

    // Если рядом есть карта, она отвечает двумя байтами типа карты (ATQA)(тип карты, например 0x04 0x00 для Mifare Classic).
    // Если ответа нет (Transceive вернул false), значит карты рядом нет — выходим.
    // Отправляются 7 бит (valid_bits = 0x07)
    if (!RC522_Transceive(dev, &req_cmd, 1, atqa, &atqa_len, 0x07U) || atqa_len != 2U) {
        return false;
    }

    // 2. Антиколлизия (чтение 4 байт UID + 1 байт контрольной суммы BCC)
    uint8_t anticoll_cmd[2] = {PICC_CMD_ANTICOLL_CL1, 0x20U};
    uint8_t resp[5] = {0};
    uint8_t resp_len = sizeof(resp);

    // Знаем что карта рядом, поэтому вызываем Transceive второй раз и отправляем 2 байта: 0x93 (выбор каскада 1) и 0x20 (запрос полного 4-байтного UID).
    // Карта присылает 5 байт: первые 4 байта — это её уникальный номер (UID), а 5-й байт — контрольная сумма BCC (побитовое исключающее ИЛИ XOR первых четырёх байт).
    // Отправляются целые байты (valid_bits = 0x00)
    if (!RC522_Transceive(dev, anticoll_cmd, 2, resp, &resp_len, 0x00U) || resp_len != 5U) {
        return false;
    }

    // Проверка контрольной суммы BCC (XOR первых 4 байт должен равняться 5-му)
    uint8_t bcc = resp[0] ^ resp[1] ^ resp[2] ^ resp[3];
    if (bcc != resp[4]) {
        return false;
    }

    for (uint8_t i = 0; i < RC522_UID_MAX_LEN; i++) {
        uid_out[i] = resp[i];
    }

    return true;
}