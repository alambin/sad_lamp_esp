#ifndef WEBSERVER_H_
#define WEBSERVER_H_

#include <array>
#include <functional>

#include <ESP8266WebServer.h>
#include <FS.h>

// TODO: This web server handles filesystem operations (list, create, delete files, etc.) inside. Better to move it
// out to new entity. But for simple UI it is fine.
class WebServer
{
public:
    enum class Event : uint8_t
    {
        RESET_WIFI_SETTINGS = 0,
        REBOOT_ESP,

        NUM_OF_EVENTS
    };
    using EventHandler = std::function<void(String const& parameters)>;

    WebServer();
    void init();
    void loop();

    void set_handler(Event event, EventHandler handler);

private:
    void reply_ok();
    void reply_ok_with_msg(String const& msg);
    void reply_ok_json_with_msg(String const& msg);
    void reply_not_found(String const& msg);
    void reply_bad_request(String const& msg);
    void reply_server_error(String const& msg);

    void handle_file_list();
    void handle_file_create();
    void delete_recursive(String const& path);
    void handle_file_delete();
    void handle_file_upload();
    bool handle_file_read(String path);
    void handle_esp_sw_upload();
    void handle_reset_wifi_settings();
    void handle_reboot_esp();

    const uint16_t                                                       port_{80};
    ESP8266WebServer                                                     web_server_;
    File                                                                 upload_file_;
    std::array<EventHandler, static_cast<uint8_t>(Event::NUM_OF_EVENTS)> handlers_;
    String                                                               esp_firmware_upload_error_;
};

#endif  // WEBSERVER_H_
