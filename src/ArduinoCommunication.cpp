#include "ArduinoCommunication.h"

#include <FS.h>

#include "IntelHexParser.h"
#include "Stk500Protocol.h"
#include "logger.h"

namespace
{
constexpr unsigned long arduino_reconnect_timeout{2000};
constexpr unsigned long default_arduino_cmd_timeout{2000};

// Do NOT use println for inter-board communication, because for ESP Serial.println() adds both:
// '\r' and '\n". So, on another side you will have to filter out '\r'
constexpr char arduino_connect_cmd[] PROGMEM              = "ESP: connect\n";
constexpr char arduino_connect_ack[] PROGMEM              = "TOESP: connect ACK";
constexpr char arduino_set_time_cmd[] PROGMEM             = "ESP: st ";
constexpr char arduino_set_time_ack[] PROGMEM             = "TOESP: st ACK";
constexpr char arduino_get_time_cmd[] PROGMEM             = "ESP: gt\n";
constexpr char arduino_get_time_ack[] PROGMEM             = "TOESP: gt ACK ";
constexpr char arduino_set_alarm_cmd[] PROGMEM            = "ESP: sa ";
constexpr char arduino_set_alarm_ack[] PROGMEM            = "TOESP: sa ACK";
constexpr char arduino_get_alarm_cmd[] PROGMEM            = "ESP: ga\n";
constexpr char arduino_get_alarm_ack[] PROGMEM            = "TOESP: ga ACK ";
constexpr char arduino_enable_alarm_cmd[] PROGMEM         = "ESP: ea ";
constexpr char arduino_enable_alarm_ack[] PROGMEM         = "TOESP: ea ACK ";
constexpr char arduino_set_sunrise_duration_cmd[] PROGMEM = "ESP: ssd ";
constexpr char arduino_set_sunrise_duration_ack[] PROGMEM = "TOESP: ssd ACK";
constexpr char arduino_get_sunrise_duration_cmd[] PROGMEM = "ESP: gsd\n";
constexpr char arduino_get_sunrise_duration_ack[] PROGMEM = "TOESP: gsd ACK ";
constexpr char arduino_set_brightness_cmd[] PROGMEM       = "ESP: sb ";
constexpr char arduino_set_brightness_ack[] PROGMEM       = "TOESP: sb ACK ";
constexpr char arduino_get_brightness_cmd[] PROGMEM       = "ESP: gb\n";
constexpr char arduino_get_brightness_ack[] PROGMEM       = "TOESP: gb ACK ";

constexpr char error_timeout[] PROGMEM = "ERROR: timeout";
constexpr char hex_chars[] PROGMEM     = "0123456789ABCDEF";

bool
flash_page(IntelHexParser& hex_parser, Stk500Protocol& stk500_protocol)
{
    byte page[128];
    hex_parser.get_memory_page(page);
    byte* load_address = hex_parser.get_load_address();
    return stk500_protocol.flash_page(load_address, page);
}
}  // namespace

ArduinoCommunication::ArduinoCommunication(WebSocketServer& web_socket_server, WebServer& web_server, uint8_t reset_pin)
  : web_socket_server_(web_socket_server)
  , web_server_(web_server)
  , buffer_{
        0,
    }
  , reset_pin_(reset_pin)
{
}

void
ArduinoCommunication::init()
{
    web_socket_server_.set_handler(WebSocketServer::Event::ARDUINO_COMMAND,
                                   [&](uint8_t client_id, String const& parameters) { send(parameters); });
    web_socket_server_.set_handler(
        WebSocketServer::Event::FLASH_ARDUINO,
        [&](uint8_t client_id, String const& parameters) { flash_arduino(client_id, parameters); });
    web_socket_server_.set_handler(WebSocketServer::Event::REBOOT_ARDUINO,
                                   [&](uint8_t client_id, String const&) { reboot_arduino(client_id); });
    web_socket_server_.set_handler(WebSocketServer::Event::GET_ARDUINO_SETTINGS,
                                   [&](uint8_t client_id, String const&) { get_arduino_settings(client_id); });
    web_socket_server_.set_handler(
        WebSocketServer::Event::ARDUINO_SET_DATETIME,
        [&](uint8_t client_id, String const& parameters) { arduino_set_datetime(client_id, parameters); });
    web_socket_server_.set_handler(
        WebSocketServer::Event::ENABLE_ARDUINO_ALARM,
        [&](uint8_t client_id, String const& parameters) { enable_arduino_alarm(client_id, parameters); });
    web_socket_server_.set_handler(
        WebSocketServer::Event::SET_ARDUINO_ALARM_TIME,
        [&](uint8_t client_id, String const& parameters) { set_arduino_alarm_time(client_id, parameters); });
    web_socket_server_.set_handler(
        WebSocketServer::Event::SET_ARDUINO_SUNRISE_DURATION,
        [&](uint8_t client_id, String const& parameters) { set_arduino_sunrise_duration(client_id, parameters); });
    web_socket_server_.set_handler(
        WebSocketServer::Event::SET_ARDUINO_BRIGHTNESS,
        [&](uint8_t client_id, String const& parameters) { set_arduino_brightness(client_id, parameters); });
}

void
ArduinoCommunication::loop()
{
    receive_line();

    while (!command_queue_.empty()) {
        auto& current_command = command_queue_.front();

        if (current_command.execution_started && (millis() >= current_command.response_timeout)) {
            DEBUG_PRINTLN(PSTR("ERROR: response timeout expired for command \"") + current_command.name + '\"');
            if (current_command.response_timeout_handler) {
                current_command.response_timeout_handler();
            }
            command_queue_.pop();
            continue;
        }
        else if (!current_command.execution_started && (millis() >= current_command.request_start_time)) {
            current_command.execution_started = true;
            current_command.response_timeout += millis();
            current_command.execute();
        }
        break;
    }
}

void
ArduinoCommunication::send(String const& message) const
{
    if (!arduino_logs_enabled_) {
        DEBUG_PRINTLN(PSTR("Arduino logging is disabled. Skip sending \"") + message + '\"');
        return;
    }

    DEBUG_PRINTLN(PSTR("TO   ARDUINO: ") + message);
    Serial.println(message);
}

void
ArduinoCommunication::receive_line()
{
    // Non-blocking read from serial port.
    while (Serial.available() > 0) {
        char ch = Serial.read();
        if (ch == '\r') {
            // Ignore this line ending. If Arduino doesn't use println() for communication with ESP,
            // it should never happen.
            continue;
        }
        if (ch != '\n') {
            if (isprint(ch)) {
                buffer_[current_buf_position_++] = ch;
                continue;
            }

            // In case of using binary-based web-socket this code is not required.
            //
            // Replace non-printable symbols with their hex codes
            // buffer_[current_buf_position_++] = '0';
            // buffer_[current_buf_position_++] = 'x';
            // buffer_[current_buf_position_++] = pgm_read_byte(&hex_chars[(ch & 0xF0) >> 4]);
            // buffer_[current_buf_position_++] = pgm_read_byte(&hex_chars[(ch & 0x0F) >> 0]);
            // buffer_[current_buf_position_++] = ' ';
            // continue;
        }

        buffer_[current_buf_position_++] = 0;
        current_buf_position_            = 0;
        String message{buffer_.data()};
        if (arduino_logs_enabled_) {
            DEBUG_PRINTLN(PSTR("FROM ARDUINO: ") + message);
        }

        if (!command_queue_.empty()) {
            auto const& current_command = command_queue_.front();
            if (current_command.execution_started) {
                if (current_command.response_handler(message)) {
                    command_queue_.pop();
                }
            }
        }
    }
}

void
ArduinoCommunication::flash_arduino(uint8_t client_id, String const& path)
{
    if (path.isEmpty() || path == "/") {
        String message{F("ERROR: invalid path")};
        DEBUG_PRINTLN(message);
        web_socket_server_.send(client_id, message);
        return;
    }

    // You can optionally disable logs from Arduino for time of flashing and subsequent reboot.
    // When text-based web-socket is used, special (non printable) characters, printed by Arduino during reboot, caused
    // web-socket crash. Currently binary-based web-socket is used, so this feature is not actual anymore.
    constexpr bool should_diable_arduino_logs{false};

    if (should_diable_arduino_logs) {
        enable_arduino_logs(false);
    }

    Serial.begin(57600);
    Serial.flush();
    while (Serial.read() != -1) {
        ;
    }

    File file{SPIFFS.open(path, "r")};
    if (!file) {
        String message{PSTR("ERROR: can not open file with Arduino firmware \"") + path + "\""};
        DEBUG_PRINTLN(message);
        web_socket_server_.send(client_id, message);
        return;
    }

    DEBUG_PRINTLN(F("Start flashing Arduino..."));
    web_socket_server_.send(client_id, F("START FLASHING"));

    Stk500Protocol stk500_protocol(&Serial, reset_pin_);
    stk500_protocol.setup_device();
    IntelHexParser hex_parser;

    while (file.available()) {
        constexpr size_t buf_len{64};
        unsigned char    buff[buf_len];
        String           line{file.readStringUntil('\n')};
        if (line.length() >= buf_len) {
            String message{PSTR("ERROR: one line of hex file is longer than ") + String(buf_len) + PSTR(" characters")};
            DEBUG_PRINTLN(message);
            web_socket_server_.send(client_id, message);
            return;
        }
        line.getBytes(buff, line.length());
        hex_parser.parse_line(buff);

        if (hex_parser.is_page_ready()) {
            if (!flash_page(hex_parser, stk500_protocol)) {
                String message{F("ERROR: flashing of Arduino failed!")};
                DEBUG_PRINTLN(message);
                web_socket_server_.send(client_id, message);
                return;
            }
        }
    }

    stk500_protocol.exit_prog_mode();
    file.close();
    Serial.begin(9600);

    DEBUG_PRINTLN(F("Flashing of Arduino is completed."));
    web_socket_server_.send(client_id, F("DONE"));

    if (should_diable_arduino_logs) {
        ArduinoCommand reconnect(
            F("reconnect"),
            [&]() { Serial.print(FPSTR(arduino_connect_cmd)); },
            [&](String const& response) {
                if (response == FPSTR(arduino_connect_ack)) {
                    DEBUG_PRINTLN(F("Arduino is connected"));
                    enable_arduino_logs(true);
                    return true;
                }
                return false;
            });
        reconnect.request_start_time = millis() + arduino_reconnect_timeout;
        reconnect.response_timeout   = default_arduino_cmd_timeout + 1000;
        command_queue_.push(reconnect);
    }
}

void
ArduinoCommunication::reboot_arduino(uint8_t client_id)
{
    String message{F("Start rebooting Arduino...")};
    DEBUG_PRINTLN(message);
    web_socket_server_.send(client_id, message);

    digitalWrite(reset_pin_, LOW);
    delay(1);
    digitalWrite(reset_pin_, HIGH);
    delay(200);

    ArduinoCommand reconnect(
        F("connect"),
        [&]() { Serial.print(FPSTR(arduino_connect_cmd)); },
        [&, client_id](String const& response) {
            if (response == FPSTR(arduino_connect_ack)) {
                DEBUG_PRINTLN(F("Arduino reboot finished"));
                web_socket_server_.send(client_id, F("DONE"));
                return true;
            }
            return false;
        });
    reconnect.response_timeout_handler = [&, client_id]() { web_socket_server_.send(client_id, FPSTR(error_timeout)); };
    reconnect.request_start_time       = 0;  // Start immediately
    reconnect.response_timeout         = default_arduino_cmd_timeout;
    command_queue_.push(reconnect);
}

void
ArduinoCommunication::get_arduino_settings(uint8_t client_id)
{
    arduino_settings_json_ = "{";

    // 1
    ArduinoCommand get_time_cmd(
        "gt",
        [&]() { Serial.print(FPSTR(arduino_get_time_cmd)); },
        [&](String const& response) {
            if (response.startsWith(FPSTR(arduino_get_time_ack))) {
                arduino_settings_json_ +=
                    PSTR("\"time\":\"") +
                    response.substring(sizeof(arduino_get_time_ack) / sizeof(arduino_get_time_ack[0]) - 1) + "\",";
                return true;
            }
            return false;
        });
    get_time_cmd.response_timeout_handler = [&]() {
        arduino_settings_json_ += String(PSTR("\"time\":\"")) + FPSTR(error_timeout) + "\",";
    };
    get_time_cmd.request_start_time = 0;  // Start immediately
    get_time_cmd.response_timeout   = default_arduino_cmd_timeout;
    command_queue_.push(get_time_cmd);

    // 2
    ArduinoCommand get_alarm_cmd(
        "ga",
        [&]() { Serial.print(FPSTR(arduino_get_alarm_cmd)); },
        [&](String const& response) {
            if (response.startsWith(FPSTR(arduino_get_alarm_ack))) {
                arduino_settings_json_ +=
                    PSTR("\"alarm\":\"") +
                    response.substring(sizeof(arduino_get_alarm_ack) / sizeof(arduino_get_alarm_ack[0]) - 1) + "\",";
                return true;
            }
            return false;
        });
    get_alarm_cmd.response_timeout_handler = [&]() {
        arduino_settings_json_ += String(PSTR("\"alarm\":\"")) + FPSTR(error_timeout) + "\",";
    };
    get_alarm_cmd.request_start_time = 0;  // Start immediately
    get_alarm_cmd.response_timeout   = default_arduino_cmd_timeout;
    command_queue_.push(get_alarm_cmd);

    // 3
    ArduinoCommand get_sunrise_duration_cmd(
        "gsd",
        [&]() { Serial.print(FPSTR(arduino_get_sunrise_duration_cmd)); },
        [&](String const& response) {
            if (response.startsWith(FPSTR(arduino_get_sunrise_duration_ack))) {
                arduino_settings_json_ +=
                    PSTR("\"sunrise duration\":\"") +
                    response.substring(
                        sizeof(arduino_get_sunrise_duration_ack) / sizeof(arduino_get_sunrise_duration_ack[0]) - 1) +
                    "\",";
                return true;
            }
            return false;
        });
    get_sunrise_duration_cmd.response_timeout_handler = [&]() {
        arduino_settings_json_ += String(PSTR("\"sunrise duration\":\"")) + FPSTR(error_timeout) + "\",";
    };
    get_sunrise_duration_cmd.request_start_time = 0;  // Start immediately
    get_sunrise_duration_cmd.response_timeout   = default_arduino_cmd_timeout;
    command_queue_.push(get_sunrise_duration_cmd);

    // 4
    ArduinoCommand get_brightness_cmd(
        "gb",
        [&]() { Serial.print(FPSTR(arduino_get_brightness_cmd)); },
        [&, client_id](String const& response) {
            if (response.startsWith(FPSTR(arduino_get_brightness_ack))) {
                arduino_settings_json_ +=
                    PSTR("\"brightness\":\"") +
                    response.substring(sizeof(arduino_get_brightness_ack) / sizeof(arduino_get_brightness_ack[0]) - 1) +
                    "\"}";
                DEBUG_PRINTLN(PSTR("Arduino settings: \"") + arduino_settings_json_ + "\"");
                web_socket_server_.send(client_id, arduino_settings_json_);
                return true;
            }
            return false;
        });
    get_brightness_cmd.response_timeout_handler = [&, client_id]() {
        arduino_settings_json_ += String(PSTR("\"brightness\":\"")) + FPSTR(error_timeout) + "\"}";
        DEBUG_PRINTLN(PSTR("Arduino settings: \"") + arduino_settings_json_ + "\"");
        web_socket_server_.send(client_id, arduino_settings_json_);
    };
    get_brightness_cmd.request_start_time = 0;  // Start immediately
    get_brightness_cmd.response_timeout   = default_arduino_cmd_timeout;
    command_queue_.push(get_brightness_cmd);
}

void
ArduinoCommunication::enable_arduino_logs(bool enable)
{
    arduino_logs_enabled_ = enable;
}

void
ArduinoCommunication::arduino_set_datetime(uint8_t client_id, String const& datetime)
{
    String         command{String(FPSTR(arduino_set_time_cmd)) + datetime + '\n'};
    ArduinoCommand set_time_cmd(
        "st",
        [command]() { Serial.print(command); },
        [&, client_id](String const& response) {
            if (response == FPSTR(arduino_set_time_ack)) {
                DEBUG_PRINTLN(F("Arduino set datetime finished"));
                web_socket_server_.send(client_id, F("DONE"));
                return true;
            }
            return false;
        });
    set_time_cmd.response_timeout_handler = [&, client_id]() {
        web_socket_server_.send(client_id, FPSTR(error_timeout));
    };
    set_time_cmd.request_start_time = 0;  // Start immediately
    set_time_cmd.response_timeout   = default_arduino_cmd_timeout;
    command_queue_.push(set_time_cmd);
}

void
ArduinoCommunication::enable_arduino_alarm(uint8_t client_id, String const& enable)
{
    String         command{String(FPSTR(arduino_enable_alarm_cmd)) + enable + '\n'};
    ArduinoCommand enable_alarm_cmd(
        "ea",
        [command]() { Serial.print(command); },
        [&](String const& response) {
            if (response.startsWith(FPSTR(arduino_enable_alarm_ack))) {
                DEBUG_PRINTLN(F("Arduino enable alarm finished"));
                web_socket_server_.send(
                    client_id,
                    response.substring(sizeof(arduino_enable_alarm_ack) / sizeof(arduino_enable_alarm_ack[0]) - 1));
                return true;
            }
            return false;
        });
    enable_alarm_cmd.response_timeout_handler = [&, client_id]() {
        web_socket_server_.send(client_id, FPSTR(error_timeout));
    };
    enable_alarm_cmd.request_start_time = 0;  // Start immediately
    enable_alarm_cmd.response_timeout   = default_arduino_cmd_timeout;
    command_queue_.push(enable_alarm_cmd);
}

void
ArduinoCommunication::set_arduino_alarm_time(uint8_t client_id, String const& time)
{
    String         command{String(FPSTR(arduino_set_alarm_cmd)) + time + '\n'};
    ArduinoCommand set_alarm_cmd(
        "sa",
        [command]() { Serial.print(command); },
        [&](String const& response) {
            if (response == FPSTR(arduino_set_alarm_ack)) {
                DEBUG_PRINTLN(F("Arduino set alarm finished"));
                web_socket_server_.send(client_id, F("DONE"));
                return true;
            }
            return false;
        });
    set_alarm_cmd.response_timeout_handler = [&, client_id]() {
        web_socket_server_.send(client_id, FPSTR(error_timeout));
    };
    set_alarm_cmd.request_start_time = 0;  // Start immediately
    set_alarm_cmd.response_timeout   = default_arduino_cmd_timeout;
    command_queue_.push(set_alarm_cmd);
}

void
ArduinoCommunication::set_arduino_sunrise_duration(uint8_t client_id, String const& sunrise_duration)
{
    String         command{String(FPSTR(arduino_set_sunrise_duration_cmd)) + sunrise_duration + '\n'};
    ArduinoCommand set_sunrise_duration(
        "ssd",
        [command]() { Serial.print(command); },
        [&](String const& response) {
            if (response == FPSTR(arduino_set_sunrise_duration_ack)) {
                DEBUG_PRINTLN(F("Arduino set sunrise duration finished"));
                web_socket_server_.send(client_id, F("DONE"));
                return true;
            }
            return false;
        });
    set_sunrise_duration.response_timeout_handler = [&, client_id]() {
        web_socket_server_.send(client_id, FPSTR(error_timeout));
    };
    set_sunrise_duration.request_start_time = 0;  // Start immediately
    set_sunrise_duration.response_timeout   = default_arduino_cmd_timeout;
    command_queue_.push(set_sunrise_duration);
}

void
ArduinoCommunication::set_arduino_brightness(uint8_t client_id, String const& brightness)
{
    String         command{String(FPSTR(arduino_set_brightness_cmd)) + brightness + '\n'};
    ArduinoCommand set_brightness(
        "sb",
        [command]() { Serial.print(command); },
        [&](String const& response) {
            if (response.startsWith(FPSTR(arduino_set_brightness_ack))) {
                DEBUG_PRINTLN(F("Arduino set brightness finished"));
                web_socket_server_.send(
                    client_id,
                    response.substring(sizeof(arduino_set_brightness_ack) / sizeof(arduino_set_brightness_ack[0]) - 1));
                return true;
            }
            return false;
        });
    set_brightness.response_timeout_handler = [&, client_id]() {
        web_socket_server_.send(client_id, FPSTR(error_timeout));
    };
    set_brightness.request_start_time = 0;  // Start immediately
    set_brightness.response_timeout   = default_arduino_cmd_timeout;
    command_queue_.push(set_brightness);
}
