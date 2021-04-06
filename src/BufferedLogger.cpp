#include "BufferedLogger.h"

BufferedLogger&
BufferedLogger::instance()
{
    static BufferedLogger buffered_logger;
    return buffered_logger;
}

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

void
BufferedLogger::setDebugOutput(bool)
{
    // Dummy implementation
}

int
BufferedLogger::available()
{
    // Dummy implementation
    return 0;
}

int
BufferedLogger::read()
{
    // Dummy implementation
    return 0;
}

int
BufferedLogger::peek()
{
    // Dummy implementation
    return 0;
}

size_t
BufferedLogger::readBytes(char*, size_t)
{
    // Dummy implementation
    return 0;
}

size_t
BufferedLogger::readBytes(uint8_t*, size_t)
{
    // Dummy implementation
    return 0;
}

String
BufferedLogger::readString()
{
    // Dummy implementation
    return "";
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
