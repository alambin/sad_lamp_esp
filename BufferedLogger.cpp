#include "BufferedLogger.h"

#include <HardwareSerial.h>

BufferedLogger::BufferedLogger(uint16_t buf_size, bool log_to_serial)
  : buf_size_{buf_size}
  , should_log_to_serial_{log_to_serial}
{
    log_.reserve(buf_size);
}

size_t
BufferedLogger::write(uint8_t c)
{
    if (should_log_to_serial_) {
        Serial.write(c);
    }

    log_ += c;
    return (shrink() ? 0 : 1);
}

size_t
BufferedLogger::write(uint8_t const* buffer, size_t size)
{
    if (should_log_to_serial_) {
        Serial.write(buffer, size);
    }

    auto shrinked = log_.concat((char const*)buffer, size);
    return size - shrinked;
}

String&
BufferedLogger::get_log()
{
    return log_;
}

void
BufferedLogger::clear()
{
    log_.clear();
}

size_t
BufferedLogger::shrink()
{
    if (log_.length() > buf_size_) {
        size_t result = log_.length() - buf_size_;
        log_          = log_.substring(result);
        return result;
    }
    return 0;
}
