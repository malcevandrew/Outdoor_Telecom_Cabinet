#ifndef LOGGER_H
#define LOGGER_H

// 1 — включить, 0 - выключить вывод логов в UART
#define ENABLE_UART_LOGS    1

#if ENABLE_UART_LOGS
    #include <stdio.h>
    void Logger_Init(void);
    #define LOG_INFO(fmt, ...)  printf("[INFO]  " fmt "\r\n", ##__VA_ARGS__)
    #define LOG_WARN(fmt, ...)  printf("[WARN]  " fmt "\r\n", ##__VA_ARGS__)
    #define LOG_ERR(fmt, ...)   printf("[ERROR] " fmt "\r\n", ##__VA_ARGS__)
#else
    #define Logger_Init()       ((void)0)
    #define LOG_INFO(fmt, ...)  ((void)0)
    #define LOG_WARN(fmt, ...)  ((void)0)
    #define LOG_ERR(fmt, ...)   ((void)0)
#endif

#endif /* LOGGER_H */