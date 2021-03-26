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
    web_server_.set_handler(WebServer::Event::FLASH_ARDUINO, [&](String const& filename) { flash_arduino(filename); });
}

void
ArduinoCommunication::loop()
{
    receive_line();

    if ((scheduled_event_time_ != 0) && (millis() >= scheduled_event_time_)) {
        scheduled_event_();
        scheduled_event_time_ = 0;
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

        if (!arduino_logs_enabled_) {
            // Enable logs to/from Arduino in case Arduino responded on connection command from ESP
            if (message == FPSTR(arduino_connect_ack)) {
                DEBUG_PRINTLN(F("Arduino is connected"));
                enable_arduino_logs(true);
            }
            continue;
        }

        DEBUG_PRINTLN(PSTR("FROM ARDUINO: ") + message);
        // TODO: implement forwarding of read data to registered handler.
    }
}

void
ArduinoCommunication::flash_arduino(String const& path)
{
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
        DEBUG_PRINTLN(PSTR("ERROR: can not open file with Arduino firmware ") + path);
        return;
    }

    DEBUG_PRINTLN(F("Start flashing Arduino..."));
    Stk500Protocol stk500_protocol(&Serial, reset_pin_);
    stk500_protocol.setup_device();
    IntelHexParser hex_parser;

    while (file.available()) {
        constexpr size_t buf_len{64};
        unsigned char    buff[buf_len];
        String           line{file.readStringUntil('\n')};
        if (line.length() >= buf_len) {
            DEBUG_PRINTLN(PSTR("ERROR: one line of hex file is longer than ") + String(buf_len) + PSTR(" characters"));
            return;
        }
        line.getBytes(buff, line.length());
        hex_parser.parse_line(buff);

        if (hex_parser.is_page_ready()) {
            if (!flash_page(hex_parser, stk500_protocol)) {
                DEBUG_PRINTLN(F("ERROR: flashing of Arduino failed!"));
                return;
            }
        }
    }

    stk500_protocol.exit_prog_mode();
    file.close();
    Serial.begin(9600);
    DEBUG_PRINTLN(F("Flashing of Arduino is completed."));

    if (should_diable_arduino_logs) {
        schedule_reconnect();
    }
}

void
ArduinoCommunication::enable_arduino_logs(bool enable)
{
    arduino_logs_enabled_ = enable;
}

void
ArduinoCommunication::schedule_reconnect()
{
    scheduled_event_      = [&]() { Serial.print(FPSTR(arduino_connect_cmd)); };
    scheduled_event_time_ = millis() + arduino_reconnect_timeout;
}