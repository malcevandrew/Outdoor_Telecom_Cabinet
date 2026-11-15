#include "lcd1602.h"
#include <string.h>

#define LCD_I2C_ADDR (0x27U << 1)   // 7 битный адрес I2C адаптера PCF8574(сразу смещен влево на 1 бит, для добавления бита чтения/записи функцией HAL)

// Распиновка I2C адаптера PCF8574 на контроллер дисплея HD44780
// PCF8574:  P7  P6  P5  P4  P3  P2  P1  P0
//           ||  ||  ||  ||  ||  ||  ||  ||
// HD44780:  D7  D6  D5  D4  BL  EN  RW  RS


#define LCD_PIN_RS          0x01U       // Режим отправки символа (устанавливаем бит №0 в 1). Если режим команды для дисплея, то бит №0 в 0 
#define LCD_PIN_RW          0x02U       // Режим чтения с дисплея (устанавливаем бит №1 в 1). Если режим запись, то бит №1 в 0
#define LCD_PIN_EN          0x04U       // Активация приема данных дисплеем (устанавливаем бит №2 в 1). 
#define LCD_PIN_BACKLIGHT   0x08U       // Включение подсветки (устанавливаем бит №3 в 1). Для выклюения бит №3 в 0

#define LCD_I2C_TIMEOUT_MS  15U
#define LCD_MAX_ERRORS      3U

// Отправка байта на адаптер PCF8574 формата [D7  D6  D5  D4  BL  EN  RW  RS],
// где старшая половина (D7-D4) это полубайт символа/команды, а младшая половина (BL, EN, RW, RS) служебные флаги
static bool LCD1602_WriteExpander(lcd1602_t *dev, uint8_t data)
{
    // Включение/выключение подсветки при необходимости
    uint8_t payload = data | (dev->backlight ? LCD_PIN_BACKLIGHT : 0x00U);  

    if (HAL_I2C_Master_Transmit(dev->hi2c, dev->dev_addr, &payload, 1, LCD_I2C_TIMEOUT_MS) != HAL_OK) {
        
        // Контроль неудачных попыток отправки
        dev->error_count++;     
        if (dev->error_count >= LCD_MAX_ERRORS) {
            dev->is_valid = false;
        }
        return false;
    }

    dev->error_count = 0;
    dev->is_valid = true;
    return true;
}

// Подготовка к отправке полубайта.
// Сначала отправляем полубайт символа/команды с EN = 1, а затем еще раз с EN = 0,
// т.к. дисплей считывает данные D4–D7 в момент спада импульса на ножке EN
static bool LCD1602_SendNibble(lcd1602_t *dev, uint8_t nibble, uint8_t rs_flag)
{
    uint8_t high_bits = (nibble & 0xF0U) | rs_flag;             

    if (!LCD1602_WriteExpander(dev, high_bits | LCD_PIN_EN)) {  // Отправка с EN = 1
        return false;
    }

    return LCD1602_WriteExpander(dev, high_bits);               // Отправка с EN = 0
}

// Подготовка к отправке байта. 
// Делим его на два полубайта и отправляем сначала старшую половину, затем младшую
// Если отправляем символ, то передаём rs_flag = LCD_PIN_RS (0x01), если команду, то rs_flag = 0 
static bool LCD1602_SendByte(lcd1602_t *dev, uint8_t byte, uint8_t rs_flag)
{
    if (!LCD1602_SendNibble(dev, byte & 0xF0U, rs_flag)) {                      // Отправляем старшую половину 
        return false;
    }
    return LCD1602_SendNibble(dev, (uint8_t)((byte << 4) & 0xF0U), rs_flag);    // Отправляем младшую половину
}

// Инициализация
bool LCD1602_Init(lcd1602_t *dev, I2C_HandleTypeDef *hi2c)
{
    if (dev == NULL || hi2c == NULL) {
        return false;
    }

    dev->hi2c = hi2c;
    dev->dev_addr = LCD_I2C_ADDR;
    dev->backlight = true;
    dev->is_valid = false;
    dev->error_count = 0;
    dev->cursor_row = 0;
    dev->cursor_col = 0;

    memset(dev->target_lines, ' ', sizeof(dev->target_lines));      // Заполняем пробелами все ячейки
    memset(dev->current_lines, ' ', sizeof(dev->current_lines));
    dev->target_lines[0][16] = '\0';    // последним символ конца строки \0
    dev->target_lines[1][16] = '\0';
    dev->current_lines[0][16] = '\0';
    dev->current_lines[1][16] = '\0';

    // Проверка готовности модуля
    if (HAL_I2C_IsDeviceReady(dev->hi2c, dev->dev_addr, 3, LCD_I2C_TIMEOUT_MS) != HAL_OK) {
        return false;
    }

    // Последовательность инициализации HD44780 в 4-битный режим
    HAL_Delay(50);
    LCD1602_SendNibble(dev, 0x30U, 0);  
    HAL_Delay(5);
    LCD1602_SendNibble(dev, 0x30U, 0);
    HAL_Delay(1);
    LCD1602_SendNibble(dev, 0x30U, 0);
    HAL_Delay(1);
    LCD1602_SendNibble(dev, 0x20U, 0); // Переход в 4-битный интерфейс
    HAL_Delay(1);

    // 2 строки, шрифт 5x8 
    if (!LCD1602_SendByte(dev, 0x28U, 0)) return false;

    // Дисплей включён, курсор выключен 
    if (!LCD1602_SendByte(dev, 0x0CU, 0)) return false;

    // Сдвиг курсора вправо при записи 
    if (!LCD1602_SendByte(dev, 0x06U, 0)) return false;
    
    // Очистка экрана 
    if (!LCD1602_SendByte(dev, 0x01U, 0)) return false;
    HAL_Delay(2);

    dev->is_valid = true;
    return true;
}

// Обработка принятой строки (разбиваем по символам)
void LCD1602_SetLine(lcd1602_t *dev, uint8_t row, const char *text)
{
    if (dev == NULL || row > 1U || text == NULL) {
        return;
    }

    size_t len = strlen(text);

    for (uint8_t i = 0; i < 16U; i++) {
        if (i < len) {
            dev->target_lines[row][i] = text[i];
        } else {
            // Дополняем строку пробелами до 16 символов, чтобы затирать символы с прошлого вывода
            dev->target_lines[row][i] = ' ';
        }
    }

    dev->target_lines[row][16] = '\0';
}

// Включение/выключение подстветки
void LCD1602_SetBacklight(lcd1602_t *dev, bool state)
{
    if (dev != NULL) {
        dev->backlight = state;
    }
}

// Обновление одного символа на дисплее
void LCD1602_Process(lcd1602_t *dev)
{
    if (dev == NULL || dev->hi2c == NULL) {
        return;
    }

    // Автовосстановление при переподключении дисплея 
    if (!dev->is_valid && dev->error_count >= LCD_MAX_ERRORS) {
        if (!LCD1602_Init(dev, dev->hi2c)) {
            return;
        }
    }

    // Поиск первого отличающегося символа и его отправка
    for (uint8_t r = 0; r < 2U; r++) {
        for (uint8_t c = 0; c < 16U; c++) {

            // Если символ отличается
            if (dev->target_lines[r][c] != dev->current_lines[r][c]) {

                // Формируем команду Set DDRAM Address(установить курсор в нужный адрес ячейки).
                // Структура по даташиту:  [1 add add add add add add add], т.е. старший бит равен 1, а остальные биты это адрес ячейки
                // Сразу учтем это. Для первой строки дисплея: 0x80 + 0x00 = 0x80.
                // Для второй строки дисплея: 0x80 + 0x40 = 0xC0U
                // Где 0x80 - это старший бит, 0x00 - начальный адрес первой строки, 0x40 - начальный адрес второй строки
                uint8_t ddram_addr = (r == 0U) ? (0x80U + c) : (0xC0U + c);     

                if (LCD1602_SendByte(dev, ddram_addr, 0) &&                                     // Отправка команды установки курсора(RS = 0)
                    LCD1602_SendByte(dev, (uint8_t)dev->target_lines[r][c], LCD_PIN_RS)) {      // Обновляем символ на этом месте (RS = 1)
                    dev->current_lines[r][c] = dev->target_lines[r][c];
                }

                return; // Выходим после обновления одного символа
            }
        }
    }
}