#ifndef DEBUGSERVER_H_
#define DEBUGSERVER_H_

#include <vector>

#include "WebSocketServer.h"

// Uses BufferedLogger singleton to get buffered logs and send them to all connected debugger clients
class DebugServer
{
public:
    explicit DebugServer(WebSocketServer& web_socket_server);
    void init();
    void loop();

private:
    void send_buffered_logs();

    WebSocketServer&     web_socket_server_;
    std::vector<uint8_t> debugger_clients_ids_;
};

#endif  // DEBUGSERVER_H_
