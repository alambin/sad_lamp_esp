#ifndef BUFFEREDLOGGER_H_
#define BUFFEREDLOGGER_H_

#include <Print.h>
#include <WString.h>

class BufferedLogger : public Print
{
public:
    BufferedLogger(uint16_t buf_size = 2 * 1024, bool log_to_serial = false);

    size_t write(uint8_t c) override;
    size_t write(uint8_t const* buffer, size_t size) override;

    String& get_log();
    void    clear();

    // Declare this function for compatibility with Serial
    void
    setDebugOutput(bool)
    {
    }

private:
    // Reduce size of log string to make it fit in allocated buffer.
    // Return amount of bytes, removed from buffer
    size_t shrink();

    String   log_;
    uint16_t buf_size_;
    bool     should_log_to_serial_;
};

#endif  // BUFFEREDLOGGER_H_
