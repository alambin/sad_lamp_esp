#include "DebugServer.h"

#include "logger.h"

DebugServer::DebugServer(WebSocketServer& web_socket_server)
  : web_socket_server_(web_socket_server)
{
}

void
DebugServer::init()
{
    web_socket_server_.init();
    web_socket_server_.set_handler(WebSocketServer::Event::DISCONNECTED, [&](uint8_t client_id) {
        debugger_clients_ids_.erase(std::remove(debugger_clients_ids_.begin(), debugger_clients_ids_.end(), client_id),
                                    debugger_clients_ids_.end());
    });
    web_socket_server_.set_handler(WebSocketServer::Event::START_READING_LOGS, [&](uint8_t client_id) {
        debugger_clients_ids_.push_back(client_id);
        send_buffered_logs();
    });
    web_socket_server_.set_handler(WebSocketServer::Event::STOP_READING_LOGS, [&](uint8_t client_id) {
        send_buffered_logs();
        debugger_clients_ids_.erase(std::remove(debugger_clients_ids_.begin(), debugger_clients_ids_.end(), client_id),
                                    debugger_clients_ids_.end());
    });

    DEBUG_PRINTLN(F("Debug server initialized"));
}

void
DebugServer::loop()
{
    static unsigned long last_print{0};
    if (millis() - last_print > 1000) {
        DEBUG_PRINT("+");  // TODO: just debug. Remove it in final version
        send_buffered_logs();
        last_print = millis();
    }

    web_socket_server_.loop();
}

void
DebugServer::send_buffered_logs()
{
    if (!debugger_clients_ids_.empty() && (BufferedLogger::instance().get_log().length() > 0)) {
        auto& log = BufferedLogger::instance().get_log();
        for (auto client_id : debugger_clients_ids_) {
            web_socket_server_.send(client_id, log);
        }
        BufferedLogger::instance().clear();
    }
}