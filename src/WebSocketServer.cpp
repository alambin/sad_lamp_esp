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
    web_socket_.sendTXT(client_id, message);
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
            handlers_[static_cast<size_t>(Event::CONNECTED)](client_id);
        }
        break;
    }

    case WStype_DISCONNECTED:
        // Websocket is disconnected
        DEBUG_PRINTF(PSTR("[%u] Disconnected!\n"), client_id);
        if (handlers_[static_cast<size_t>(Event::DISCONNECTED)] != nullptr) {
            handlers_[static_cast<size_t>(Event::DISCONNECTED)](client_id);
        }
        break;

    case WStype_TEXT:
        // New text data is received
        DEBUG_PRINTF(PSTR("[%u] Received text: %s\n"), client_id, payload);
        String command((char const*)payload);
        if ((command == F("start_reading_logs")) &&
            (handlers_[static_cast<size_t>(Event::START_READING_LOGS)] != nullptr)) {
            handlers_[static_cast<size_t>(Event::START_READING_LOGS)](client_id);
        }
        else if ((command == F("stop_reading_logs")) &&
                 (handlers_[static_cast<size_t>(Event::STOP_READING_LOGS)] != nullptr)) {
            handlers_[static_cast<size_t>(Event::STOP_READING_LOGS)](client_id);
        }
        break;
    }
}
