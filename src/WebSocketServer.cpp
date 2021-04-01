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
WebSocketServer::send(uint8_t client_id, String const& message)
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
WebSocketServer::trigger_event(uint8_t client_id, String const& input_data, String const& command_name, Event event)
{
    if (input_data.length() <= (command_name.length() + 1)) {
        DEBUG_PRINTLN(PSTR("ERROR: command \"") + command_name + F("\" doesn't have parameters"));
        return;
    }

    DEBUG_PRINTLN(PSTR("Received command \"") + input_data + "\"");
    auto parameters = input_data.substring(command_name.length() + 1);
    if (handlers_[static_cast<size_t>(event)] != nullptr) {
        handlers_[static_cast<size_t>(event)](client_id, parameters);
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
    else if (command == F("reboot_arduino")) {
        DEBUG_PRINTLN(PSTR("Received command \"") + command + "\"");
        if (handlers_[static_cast<size_t>(Event::REBOOT_ARDUINO)] != nullptr) {
            handlers_[static_cast<size_t>(Event::REBOOT_ARDUINO)](client_id, "");
        }
        return;
    }
    else if (command == F("get_arduino_settings")) {
        DEBUG_PRINTLN(PSTR("Received command \"") + command + "\"");
        if (handlers_[static_cast<size_t>(Event::GET_ARDUINO_SETTINGS)] != nullptr) {
            handlers_[static_cast<size_t>(Event::GET_ARDUINO_SETTINGS)](client_id, "");
        }
        return;
    }

    String arduino_command_str{F("arduino_command")};
    String upload_arduino_firmware_str{F("upload_arduino_firmware")};
    String set_arduino_datetime_str{F("set_arduino_datetime")};
    String enable_arduino_alarm_str{F("enable_arduino_alarm")};
    String set_arduino_alarm_time_str{F("set_arduino_alarm_time")};
    String set_arduino_sunrise_duration_str{F("set_arduino_sunrise_duration")};
    String set_arduino_brightness_str{F("set_arduino_brightness")};
    if (command.startsWith(arduino_command_str)) {
        trigger_event(client_id, command, arduino_command_str, Event::ARDUINO_COMMAND);
        return;
    }
    else if (command.startsWith(set_arduino_datetime_str)) {
        trigger_event(client_id, command, set_arduino_datetime_str, Event::ARDUINO_SET_DATETIME);
        return;
    }
    else if (command.startsWith(enable_arduino_alarm_str)) {
        trigger_event(client_id, command, enable_arduino_alarm_str, Event::ENABLE_ARDUINO_ALARM);
        return;
    }
    else if (command.startsWith(set_arduino_alarm_time_str)) {
        trigger_event(client_id, command, set_arduino_alarm_time_str, Event::SET_ARDUINO_ALARM_TIME);
        return;
    }
    else if (command.startsWith(set_arduino_sunrise_duration_str)) {
        trigger_event(client_id, command, set_arduino_sunrise_duration_str, Event::SET_ARDUINO_SUNRISE_DURATION);
        return;
    }
    else if (command.startsWith(set_arduino_brightness_str)) {
        trigger_event(client_id, command, set_arduino_brightness_str, Event::SET_ARDUINO_BRIGHTNESS);
        return;
    }
    else if (command.startsWith(upload_arduino_firmware_str)) {
        if (command.length() <= (upload_arduino_firmware_str.length() + 1)) {
            String message{F("ERROR: command \"upload_arduino_firmware\" doesn't have parameters")};
            DEBUG_PRINTLN(message);
            send(client_id, message);
            return;
        }

        auto first_quote_position  = upload_arduino_firmware_str.length() + 1;
        auto second_quote_position = command.indexOf('"', first_quote_position + 1);
        if (second_quote_position == -1) {
            String message{F("ERROR: command \"upload_arduino_firmware\" should have \"path\" parameter in quotes")};
            DEBUG_PRINTLN(message);
            send(client_id, message);
            return;
        }

        DEBUG_PRINTLN(PSTR("Received command \"") + command + "\"");
        auto path = command.substring(first_quote_position + 1, second_quote_position);
        if (handlers_[static_cast<size_t>(Event::FLASH_ARDUINO)] != nullptr) {
            handlers_[static_cast<size_t>(Event::FLASH_ARDUINO)](client_id, path);
        }
        return;
    }

    DEBUG_PRINTLN(PSTR("ERROR: received unknown command \"") + command + "\"");
}