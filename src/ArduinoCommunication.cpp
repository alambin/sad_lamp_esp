#include "ArduinoCommunication.h"

#include <FS.h>

#include "IntelHexParser.h"
#include "Stk500Protocol.h"
#include "logger.h"

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
    // TODO: test it
    Serial.begin(57600);
    // Serial.swap();
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
        byte   buff[50];
        String data{file.readStringUntil('\n')};
        data.getBytes(buff, data.length());
        hex_parser.parse_line(buff);

        if (hex_parser.is_page_ready()) {
            byte* page    = hex_parser.get_memory_page();
            byte* address = hex_parser.get_load_address();
            stk500_protocol.flash_page(address, page);
        }
    }

    stk500_protocol.exit_prog_mode();
    file.close();
    Serial.begin(9600);
    DEBUG_PRINTLN(F("Flashing of Arduino is completed."));
}