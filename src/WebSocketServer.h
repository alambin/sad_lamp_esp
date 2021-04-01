#ifndef WEBSOCKETSERVER_H_
#define WEBSOCKETSERVER_H_

#include <array>

#include <WebSocketsServer.h>

// Facade for communication over WebSocket. Can be used by another servers to implement their functionality
class WebSocketServer
{
public:
    enum class Event : uint8_t
    {
        CONNECTED = 0,
        DISCONNECTED,
        START_READING_LOGS,
        STOP_READING_LOGS,
        ARDUINO_COMMAND,
        FLASH_ARDUINO,
        REBOOT_ARDUINO,
        GET_ARDUINO_SETTINGS,
        ARDUINO_SET_DATETIME,
        ENABLE_ARDUINO_ALARM,
        SET_ARDUINO_ALARM_TIME,
        SET_ARDUINO_SUNRISE_DURATION,
        SET_ARDUINO_BRIGHTNESS,

        NUM_OF_EVENTS
    };
    using EventHandler = std::function<void(uint8_t client_id, String const& parameters)>;

    WebSocketServer();
    void init();
    void loop();
    void set_handler(Event event, EventHandler handler);

    // Send to all connected clients
    void send(uint8_t client_id, String const& message);

private:
    void on_event(uint8_t client_id, WStype_t event_type, uint8_t* payload, size_t lenght);
    void trigger_event(uint8_t client_id, String const& input_data, String const& command_name, Event event);
    void process_command(uint8_t client_id, String const& command);

    const uint16_t                                                       port_{81};
    WebSocketsServer                                                     web_socket_;
    std::array<EventHandler, static_cast<uint8_t>(Event::NUM_OF_EVENTS)> handlers_;
};


#endif  // WEBSOCKETSERVER_H_
