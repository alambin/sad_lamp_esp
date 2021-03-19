#include "BufferedLogger.h"

BufferedLogger::BufferedLogger(uint16_t buf_size)
  : buf_size_{buf_size}
{
    log_.reserve(buf_size);
}

size_t
BufferedLogger::write(uint8_t c)
{
    log_ += c;
    return (shrink() ? 0 : 1);
}

size_t
BufferedLogger::write(uint8_t const* buffer, size_t size)
{
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
