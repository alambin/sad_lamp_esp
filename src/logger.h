#ifndef LOGGER_H_
#define LOGGER_H_

//#define DBG_OUTPUT_PORT Serial
#define DBG_OUTPUT_PORT Web

// Set it if you want to log to both: web page and Serial
// It should be always disabled if ESP is connected to Arduino. Otherwise ESP will send a lot of logs into Arduino
constexpr bool should_log_to_serial = false; 

#if DBG_OUTPUT_PORT == Serial
#define DGB_STREAM Serial
#endif

#if DBG_OUTPUT_PORT == Web
#include "BufferedLogger.h"
#define DGB_STREAM BufferedLogger::instance()
#endif

#define DEBUG_PRINT(msg)          \
    {                             \
        DGB_STREAM.print(msg);    \
        if (should_log_to_serial) \
            Serial.print(msg);    \
    }
#define DEBUG_PRINTLN(msg)        \
    {                             \
        DGB_STREAM.println(msg);  \
        if (should_log_to_serial) \
            Serial.println(msg);  \
    }
#define DEBUG_PRINTF(...)                 \
    {                                     \
        DGB_STREAM.printf_P(__VA_ARGS__); \
        if (should_log_to_serial)         \
            Serial.printf_P(__VA_ARGS__); \
    }

#endif  // LOGGER_H_