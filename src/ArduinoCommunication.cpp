#include "ArduinoCommunication.h"

#include <FS.h>

#include "IntelHexParser.h"
#include "Stk500Protocol.h"
#include "logger.h"

namespace
{
constexpr unsigned long arduino_reconnect_timeout{2000};

// Do NOT use println for inter-board communication, because for ESP Serial.println() adds both:
// '\r' and '\n". So, on another side you will have to filter out '\r'
constexpr char arduino_connect_cmd[] PROGMEM = "ESP: CONNECT\n";
constexpr char arduino_connect_ack[] PROGMEM = "ARDUINO CONNECT ACK";
constexpr char hex_chars[] PROGMEM           = "0123456789ABCDEF";

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

    // web_server_.set_handler(WebServer::Event::FLASH_ARDUINO, [&](String const& filename) { flash_arduino(filename);
    // }); web_server_.set_handler(WebServer::Event::REBOOT_ARDUINO, [&](String const&) { reboot_arduino(); });
}

void
ArduinoCommunication::loop()
{
    receive_line();

    // if ((scheduled_event_time_ != 0) && (millis() >= scheduled_event_time_)) {
    //     scheduled_event_();
    //     scheduled_event_time_ = 0;
    // }
    while (!command_queue_.empty()) {
        auto& current_command = command_queue_.front();

        if (current_command.execution_started && (millis() >= current_command.response_timeout)) {
            DEBUG_PRINTLN(PSTR("ERROR: response timeout expired for command ") + current_command.name);
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
        reconnect.response_timeout   = 3000;
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
        F("reconnect"),
        [&]() { Serial.print(FPSTR(arduino_connect_cmd)); },
        [&](String const& response) {
            if (response == FPSTR(arduino_connect_ack)) {
                DEBUG_PRINTLN(F("Arduino reboot finished"));
                web_socket_server_.send(client_id, F("DONE"));
                return true;
            }
            return false;
        });
    reconnect.request_start_time = millis() + arduino_reconnect_timeout;
    reconnect.response_timeout   = 3000;
    // response_timeout_handler - TODO
    command_queue_.push(reconnect);
}

void
ArduinoCommunication::enable_arduino_logs(bool enable)
{
    arduino_logs_enabled_ = enable;
}
