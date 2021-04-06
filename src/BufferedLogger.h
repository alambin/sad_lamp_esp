#ifndef BUFFEREDLOGGER_H_
#define BUFFEREDLOGGER_H_

#include <Stream.h>
#include <WString.h>

class BufferedLogger : public Stream
{
public:
    // Singleton
    static BufferedLogger& instance();
    BufferedLogger(BufferedLogger&)  = delete;
    BufferedLogger(BufferedLogger&&) = delete;
    BufferedLogger& operator=(BufferedLogger&) = delete;
    BufferedLogger& operator=(BufferedLogger&&) = delete;

    size_t write(uint8_t c) override;
    size_t write(uint8_t const* buffer, size_t size) override;

    String& get_log();
    void    clear();

    // Declare this function for compatibility with Serial
    void setDebugOutput(bool);

    // Implement dummy Stream interface operations. Right now BufferedLogger is casted to Stream only in WiFiManager,
    // which, in fact, doesn't use any of these Stream functions
    int    available() override;
    int    read() override;
    int    peek() override;
    size_t readBytes(char*, size_t) override;
    size_t readBytes(uint8_t*, size_t) override;
    String readString() override;

private:
    explicit BufferedLogger(uint16_t buf_size = 2 * 1024);

    // Reduce size of log string to make it fit in allocated buffer.
    // Return amount of bytes, removed from buffer
    size_t shrink();

    String   log_;
    uint16_t buf_size_;
};

#endif  // BUFFEREDLOGGER_H_
