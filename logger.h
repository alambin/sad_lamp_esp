#ifndef LOGGER_H_
#define LOGGER_H_

//#define DBG_OUTPUT_PORT Serial
#define DBG_OUTPUT_PORT buffered_logger
constexpr bool should_log_to_serial = true;  // Set it if you want to log to both: web page and Serial
#define DEBUG_PRINT(msg)            \
    {                               \
        DBG_OUTPUT_PORT.print(msg); \
        if (should_log_to_serial)   \
            Serial.print(msg);      \
    }
#define DEBUG_PRINTLN(msg)            \
    {                                 \
        DBG_OUTPUT_PORT.println(msg); \
        if (should_log_to_serial)     \
            Serial.println(msg);      \
    }
#define DEBUG_PRINTF(...)                      \
    {                                          \
        DBG_OUTPUT_PORT.printf_P(__VA_ARGS__); \
        if (should_log_to_serial)              \
            Serial.printf_P(__VA_ARGS__);      \
    }

#if DBG_OUTPUT_PORT == buffered_logger
#include "BufferedLogger.h"
BufferedLogger buffered_logger(2 * 1024);
#endif

#endif  // LOGGER_H_