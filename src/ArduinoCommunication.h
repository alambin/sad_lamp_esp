#ifndef ARDUINOCOMMUNICATION_H_
#define ARDUINOCOMMUNICATION_H_

#include <array>
#include <queue>

#include <WString.h>

#include "ArduinoCommand.h"
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
    void flash_arduino(uint8_t client_id, String const& path);
    void reboot_arduino(uint8_t client_id);
    void get_arduino_settings(uint8_t client_id);
    void enable_arduino_logs(bool enable);
    void arduino_set_datetime(uint8_t client_id, String const& datetime);
    void enable_arduino_alarm(uint8_t client_id, String const& enable);
    void set_arduino_alarm_time(uint8_t client_id, String const& time);
    void set_arduino_sunrise_duration(uint8_t client_id, String const& sunrise_duration);
    void set_arduino_brightness(uint8_t client_id, String const& brightness);

    WebSocketServer&               web_socket_server_;
    WebServer&                     web_server_;
    bool                           is_connected_{false};
    static constexpr uint16_t      buffer_size_{256};
    std::array<char, buffer_size_> buffer_;
    uint16_t                       current_buf_position_{0};
    uint8_t                        reset_pin_;
    bool                           arduino_logs_enabled_{true};

    std::queue<ArduinoCommand> command_queue_;
    String                     arduino_settings_json_;
};

#endif  // ARDUINOCOMMUNICATION_H_
