#ifndef ARDUINOCOMMUNICATION_H_
#define ARDUINOCOMMUNICATION_H_

#include <array>

#include <WString.h>

#include "WebServer.h"
#include "WebSocketServer.h"

class ArduinoCommunication
{
public:
    explicit ArduinoCommunication(WebSocketServer& web_socket_server, WebServer& web_server, uint8_t reset_pin);
    void init();
    void loop();

    void send(String const& message) const;

private:
    void receive_line();
    void flash_arduino(String const& path);
    void enable_arduino_logs(bool enable);
    void schedule_reconnect();

    WebSocketServer&               web_socket_server_;
    WebServer&                     web_server_;
    bool                           is_connected_{false};
    static constexpr uint16_t      buffer_size_{256};
    std::array<char, buffer_size_> buffer_;
    uint16_t                       current_buf_position_{0};
    uint8_t                        reset_pin_;
    bool                           arduino_logs_enabled_{true};

    std::function<void()> scheduled_event_{nullptr};
    unsigned long         scheduled_event_time_{0};
};

#endif  // ARDUINOCOMMUNICATION_H_
