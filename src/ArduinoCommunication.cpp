#include "ArduinoCommunication.h"

#include <FS.h>

#include "IntelHexParser.h"
#include "Stk500Protocol.h"
#include "logger.h"

namespace
{
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
}

void
ArduinoCommunication::send(String const& message) const
{
    DEBUG_PRINTLN(PSTR("TO   ARDUINO: ") + message);
    Serial.println(message);
}

void
ArduinoCommunication::receive_line()
{
    // Non-blocking read from serial port.
    while (Serial.available() > 0) {
        char ch = Serial.read();
        if (ch != '\n') {
            buffer_[current_buf_position_++] = ch;
            continue;
        }

        buffer_[current_buf_position_++] = 0;
        current_buf_position_            = 0;
        String message{buffer_.data()};
        DEBUG_PRINT(PSTR("FROM ARDUINO: ") + message);
        // TODO: implement forwarding of read data to registered handler.
    }
}

void
ArduinoCommunication::flash_arduino(String const& path) const
{
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
}