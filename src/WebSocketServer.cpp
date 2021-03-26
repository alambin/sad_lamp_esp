#include "WebSocketServer.h"

#include "logger.h"

WebSocketServer::WebSocketServer()
  : web_socket_{port_}
  , handlers_{
        nullptr,
    }
{
}

void
WebSocketServer::init()
{
    web_socket_.begin();
    web_socket_.onEvent([this](uint8_t client_num, WStype_t event_type, uint8_t* payload, size_t lenght) {
        on_event(client_num, event_type, payload, lenght);
    });
}

void
WebSocketServer::loop()
{
    web_socket_.loop();
}

void
WebSocketServer::set_handler(Event event, EventHandler handler)
{
    handlers_[static_cast<size_t>(event)] = handler;
}

void
WebSocketServer::send(uint8_t client_id, String& message)
{
    // Use sendBIN() instead of sendTXT(). Binary-based communication let transfering special characters.
    // Ex. Arduino when rebooted can send via Serial port some special (non printable) characters. It ruins text-based
    // web-socket but binary-based web-socket handles it well.
    web_socket_.sendBIN(client_id, (const uint8_t*)message.c_str(), message.length());
}

void
WebSocketServer::on_event(uint8_t client_id, WStype_t event_type, uint8_t* payload, size_t lenght)
{
    switch (event_type) {
    case WStype_CONNECTED: {
        // New websocket connection is established
        IPAddress ip = web_socket_.remoteIP(client_id);
        DEBUG_PRINTF(PSTR("[%u] Connected from %d.%d.%d.%d url: %s\n"), client_id, ip[0], ip[1], ip[2], ip[3], payload);
        if (handlers_[static_cast<size_t>(Event::CONNECTED)] != nullptr) {
            handlers_[static_cast<size_t>(Event::CONNECTED)](client_id, "");
        }
        break;
    }

    case WStype_DISCONNECTED:
        // Websocket is disconnected
        DEBUG_PRINTF(PSTR("[%u] Disconnected!\n"), client_id);
        if (handlers_[static_cast<size_t>(Event::DISCONNECTED)] != nullptr) {
            handlers_[static_cast<size_t>(Event::DISCONNECTED)](client_id, "");
        }
        break;

    case WStype_TEXT:
        // New text data is received
        String command((char const*)payload);
        process_command(client_id, command);
        break;
    }
}

void
WebSocketServer::process_command(uint8_t client_id, String const& command)
{
    if (command == F("start_reading_logs")) {
        DEBUG_PRINTLN(PSTR("Received command \"") + command + "\"");
        if (handlers_[static_cast<size_t>(Event::START_READING_LOGS)] != nullptr) {
            handlers_[static_cast<size_t>(Event::START_READING_LOGS)](client_id, "");
        }
        return;
    }
    else if (command == F("stop_reading_logs")) {
        DEBUG_PRINTLN(PSTR("Received command \"") + command + "\"");
        if (handlers_[static_cast<size_t>(Event::STOP_READING_LOGS)] != nullptr) {
            handlers_[static_cast<size_t>(Event::STOP_READING_LOGS)](client_id, "");
        }
        return;
    }

    String arduino_command_str{"arduino_command"};
    if (command.startsWith(arduino_command_str)) {
        if (command.length() <= (arduino_command_str.length() + 1)) {
            DEBUG_PRINTLN("ERROR: command \"arduino_command\" doesn't have parameters");
            return;
        }

        DEBUG_PRINTLN(PSTR("Received command \"") + command + "\"");
        auto parameters = command.substring(arduino_command_str.length() + 1);
        if (handlers_[static_cast<size_t>(Event::ARDUINO_COMMAND)] != nullptr) {
            handlers_[static_cast<size_t>(Event::ARDUINO_COMMAND)](client_id, parameters);
        }
        return;
    }

    DEBUG_PRINTLN(PSTR("ERROR: received unknown command \"") + command + "\"");
}