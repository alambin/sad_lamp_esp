#include <map>

#include <ESP8266SSDP.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <FTPServer.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>

#include "logger.h"


// TODO: refactor it in C++ way. TAKE INTO ACCOUNT classes in esp_avr_programmer.
// So, after refactoring this project will be easily merged with esp_avr_programmer


// FTP server consumes 1% of flash and 2% of RAM
// This is IMPORTANT: for details how to configure FTP-client visit https://github.com/nailbuster/esp8266FTPServer
// In particular:
// 0. Take more stable fork: https://github.com/charno/FTPClientServer
// 1. You need to setup Filezilla(or other client) to only allow 1 connection
// 2. It does NOT support any encryption, so you'll have to disable any form of encryption
// 3. It only supports Passive ftp mode
// 4, Look at "Issues" folder - looks likr this library is quite unstable
#define FTP_SERVER

namespace
{
ESP8266WebServer web_server(80);
WebSocketsServer web_socket(81);
File             upload_file;
bool             debugger_client_connected{false};
uint8_t          debugger_client_number{0};

#ifdef FTP_SERVER
FTPServer ftp_server(SPIFFS);
#endif

static const char TEXT_PLAIN[] PROGMEM     = "text/plain";
static const char TEXT_JSON[] PROGMEM      = "text/json";
static const char FS_INIT_ERROR[] PROGMEM  = "FS INIT ERROR";
static const char FILE_NOT_FOUND[] PROGMEM = "FileNotFound";

// Utils to return HTTP codes, and determine content-type

void
reply_ok()
{
    web_server.send(200, FPSTR(TEXT_PLAIN), "");
}

void
reply_ok_with_msg(String const& msg)
{
    web_server.send(200, FPSTR(TEXT_PLAIN), msg);
}

void
reply_ok_json_with_msg(String const& msg)
{
    web_server.send(200, FPSTR(TEXT_JSON), msg);
}

void
reply_not_found(String const& msg)
{
    web_server.send(404, FPSTR(TEXT_PLAIN), msg);
}

void
reply_bad_request(String const& msg)
{
    DEBUG_PRINTLN(msg);
    web_server.send(400, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

void
reply_server_error(String const& msg)
{
    DEBUG_PRINTLN(msg);
    web_server.send(500, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

String
check_for_unsupported_path(String const& filename)
{
    String error;
    if (!filename.startsWith("/")) {
        error += F("!NO_LEADING_SLASH! ");
    }
    if (filename.indexOf("//") != -1) {
        error += F("!DOUBLE_SLASH! ");
    }
    if (filename.endsWith("/")) {
        error += F("!TRAILING_SLASH! ");
    }
    return error;
}

/*
   As some FS (e.g. LittleFS) delete the parent folder when the last child has been removed,
   return the path of the closest parent still existing
*/
String
last_existing_parent(String path)
{
    while (!path.isEmpty() && !SPIFFS.exists(path)) {
        if (path.lastIndexOf('/') > 0) {
            path = path.substring(0, path.lastIndexOf('/'));
        }
        else {
            path.clear();  // No slash => the top folder does not exist
        }
    }
    DEBUG_PRINTLN(PSTR("Last existing parent: ") + path);
    return path;
}

bool
case_insensitive_string_less(String const& str1, String const& str2)
{
    // if (str1.length() < str2.length()) {
    //     return true;
    // }
    // if (str2.length() < str1.length()) {
    //     return false;
    // }
    auto min_size = (str1.length() < str2.length()) ? str1.length() : str2.length();
    for (size_t i = 0; i < min_size; ++i) {
        if (std::toupper(str1[i]) != std::toupper(str2[i])) {
            return std::toupper(str1[i]) < std::toupper(str2[i]);
        }
    }
    return str1.length() < str2.length();
}

auto filename_less = [](String const& l, String const& r) {
    // return std::lexicographical_compare(l.begin(), l.end(), r.begin(), r.end());
    if ((l[0] == '/') && (r[0] != '/')) {
        // "l" is directory
        return true;
    }
    else if ((r[0] == '/') && (l[0] != '/')) {
        // "r" is directory
        return false;
    }
    else if ((l[0] == '/') && (r[0] == '/')) {
        // Both strings are directory names
        return case_insensitive_string_less(l.substring(1), r.substring(1));
    }
    // Regular file names
    return case_insensitive_string_less(l, r);
};

}  // namespace

void
setup()
{
    WiFi.mode(WIFI_STA);  // explicitly set mode, esp defaults to STA+AP

    Serial.begin(9600);
    while (!Serial) {
        ;
    }
    delay(3000);

    // BUG: by some reason when WiFi connects in STA mode ("station"), it ruins WiFi on router
    // Idea1. Reduce TX power of ESP.
    // Workaround - connect ESP to Access Point of mobile phone
    init_wifi();

    SPIFFS.begin();
    web_socket_server_init();
    web_server_init();
    SSDP_init();

#ifdef FTP_SERVER
    // Setup the ftp server with username and password
    // ports are defined in FTPCommon.h, default is
    //   21 for the control connection
    //   50009 for the data connection (passive mode by default)
    ftp_server.begin(F("esp8266"), F("esp8266"));
    DEBUG_PRINTLN(F("FTP server initialized"));
#endif
}

void
loop()
{
    static unsigned long last_print{0};
    if (millis() - last_print > 1000) {
        DEBUG_PRINT("+");  // TODO: just debug. Remove it in final version
        send_debug_logs();
        last_print = millis();
    }

    web_socket.loop();
    web_server.handleClient();

#ifdef FTP_SERVER
    ftp_server.handleFTP();
#endif
}

void
configModeCallback(WiFiManager* wifiManager)
{
    DEBUG_PRINT(F("Could not connect to Access Point. Entering config mode. Connect to \""));
    DEBUG_PRINT(wifiManager->getConfigPortalSSID());
    DEBUG_PRINT(F("\" WiFi network and open http://"));
    DEBUG_PRINT(WiFi.softAPIP());
    DEBUG_PRINTLN(F(" (if it didn't happen automatically) to configure WiFi settings of SAD-lamp"));
}

void
init_wifi()
{
    // WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    // For debug info
    // DBG_OUTPUT_PORT.setDebugOutput(true);
    // wifiManager.setDebugOutput(true);

    // set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
    wifiManager.setAPCallback(configModeCallback);

    // Reset settings - wipe credentials for testing
    // wifiManager.resetSettings();

    // Aautomatically connect using saved credentials if they exist
    // If connection fails it starts an access point with the specified name (here  "SAD-Lamp_AP"). If name is empty,
    // it will auto generate it in format "ESP-chipid". If password is blank it will be anonymous
    // And goes into a blocking loop awaiting configuration
    // If access point "SAD-Lamp_AP" is established, it's configuration is available by address http://192.168.4.1
    if (!wifiManager.autoConnect(PSTR("SAD-Lamp_AP") /*, "password"*/)) {
        DEBUG_PRINTLN(F("failed to connect and hit timeout"));
        delay(3000);
        // if we still have not connected restart and try all over again
        ESP.restart();
        delay(5000);
    }

    DEBUG_PRINT(F("Connected to Access Point \""));
    DEBUG_PRINT(WiFi.SSID());
    DEBUG_PRINT(F("\". IP: "));
    DEBUG_PRINT(WiFi.localIP().toString());
    DEBUG_PRINT(F("; mask: "));
    DEBUG_PRINT(WiFi.subnetMask().toString());
    DEBUG_PRINT(F("; gateway: "));
    DEBUG_PRINT(WiFi.gatewayIP().toString());
    DEBUG_PRINTLN();
}

void
SPIFFS_init()
{
}

void
SSDP_init()
{
    SSDP.setDeviceType(F("upnp:rootdevice"));
    SSDP.setSchemaURL(F("description.xml"));
    SSDP.setHTTPPort(80);
    SSDP.setName(F("SAD-Lamp"));
    SSDP.setSerialNumber("1");
    SSDP.setURL("/");
    SSDP.setModelName(F("SAD-Lamp"));
    SSDP.setModelNumber("1");
    // SSDP.setModelURL("http://adress.ru/page/");
    SSDP.setManufacturer(F("Lambin Alexey"));
    // SSDP.setManufacturerURL("http://www.address.ru");
    SSDP.begin();

    DEBUG_PRINTLN(F("SSDP initialized"));
}

void
web_socket_server_init()
{
    web_socket.begin();
    web_socket.onEvent(web_socket_event_handler);
    DEBUG_PRINTLN(F("WebSocket server started."));
}

void
web_socket_event_handler(uint8_t client_num, WStype_t event_type, uint8_t* payload, size_t lenght)
{
    switch (event_type) {
    case WStype_DISCONNECTED:
        // Websocket is disconnected
        DEBUG_PRINTF(PSTR("[%u] Disconnected!\n"), client_num);
        debugger_client_connected = false;
        break;
    case WStype_CONNECTED: {
        // New websocket connection is established
        IPAddress ip = web_socket.remoteIP(client_num);
        DEBUG_PRINTF(
            PSTR("[%u] Connected from %d.%d.%d.%d url: %s\n"), client_num, ip[0], ip[1], ip[2], ip[3], payload);
        break;
    }
    case WStype_TEXT:
        // New text data is received
        DEBUG_PRINTF(PSTR("[%u] Received text: %s\n"), client_num, payload);
        String command((char const*)payload);
        process_websocket_command(command, client_num);
        break;
    }
}

void
web_server_init()
{
    // SSDP description
    web_server.on(F("/description.xml"), HTTP_GET, []() { SSDP.schema(web_server.client()); });

    // HTTP pages to work with file system
    // List directory
    web_server.on(F("/list"), HTTP_GET, handle_file_list);
    // Load editor
    web_server.on(F("/edit"), HTTP_GET, []() {
        if (!handle_file_read(F("/edit.htm"))) {
            reply_not_found(FPSTR(FILE_NOT_FOUND));
        }
    });
    // Create file
    web_server.on(F("/edit"), HTTP_PUT, handle_file_create);
    // Delete file
    web_server.on(F("/edit"), HTTP_DELETE, handle_file_delete);

    // Upload file
    // - first callback is called after the request has ended with all parsed arguments
    // - second callback handles file upload at that location
    web_server.on(
        F("/edit"), HTTP_POST, []() { reply_ok(); }, handle_file_upload);

    // Called when the url is not defined here
    // Use it to load content from SPIFFS
    web_server.onNotFound([]() {
        if (!handle_file_read(web_server.uri())) {
            reply_not_found(FPSTR(FILE_NOT_FOUND));
        }
    });

    // Update ESP firmware
    web_server.on(
        F("/update"),
        HTTP_POST,
        []() {
            web_server.sendHeader(F("Connection"), F("close"));
            web_server.sendHeader(F("Access-Control-Allow-Origin"), "*");
            reply_ok_with_msg(Update.hasError() ? F("FAIL") : F("OK"));
            ESP.restart();
        },
        habdle_esp_sw_upload);
    // web_server.on("/get_esp_sw_upload_progress", handle_get_esp_sw_upload_progress);

    web_server.begin();
    DEBUG_PRINTLN(F("Web server initialized"));
}

void
habdle_esp_sw_upload()
{
    HTTPUpload& upload = web_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        DBG_OUTPUT_PORT.setDebugOutput(true);
        WiFiUDP::stopAll();
        DEBUG_PRINTF(PSTR("Update: %s\n"), upload.filename.c_str());
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace)) {  // start with max available size
            Update.printError(DBG_OUTPUT_PORT);
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(DBG_OUTPUT_PORT);
        }
    }
    else if (upload.status == UPLOAD_FILE_END) {
        reply_ok_with_msg(F("ESP firmware update completed!"));
        if (Update.end(true)) {  // true to set the size to the current progress
            DEBUG_PRINTF(PSTR("Update Success: %u\nRebooting...\n"), upload.totalSize);
        }
        else {
            Update.printError(DBG_OUTPUT_PORT);
        }
        DBG_OUTPUT_PORT.setDebugOutput(false);
    }
    yield();
}

bool
handle_file_read(String path)
{
    DEBUG_PRINTLN(PSTR("handle_file_read: ") + path);

    if (path.endsWith("/")) {
        path += F("index.htm");
    }

    String contentType;
    if (web_server.hasArg(F("download"))) {
        contentType = F("application/octet-stream");
    }
    else {
        contentType = mime::getContentType(path);
    }

    if (!SPIFFS.exists(path)) {
        // File not found, try gzip version
        path = path + F(".gz");
    }
    if (SPIFFS.exists(path)) {
        File file = SPIFFS.open(path, "r");
        if (web_server.streamFile(file, contentType) != file.size()) {
            DEBUG_PRINTLN(F("Sent less data than expected!"));
        }
        file.close();
        return true;
    }

    return false;
}

void
handle_file_upload()
{
    if (web_server.uri() != F("/edit")) {
        return;
    }

    HTTPUpload& upload = web_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        // Make sure paths always start with "/"
        if (!filename.startsWith("/")) {
            filename = "/" + filename;
        }
        DEBUG_PRINTLN(PSTR("handle_file_upload Name: ") + filename);
        upload_file = SPIFFS.open(filename, "w");
        if (!upload_file) {
            return reply_server_error(F("CREATE FAILED"));
        }
        DEBUG_PRINTLN(PSTR("Upload: START, filename: ") + filename);
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
        if (upload_file) {
            size_t bytesWritten = upload_file.write(upload.buf, upload.currentSize);
            if (bytesWritten != upload.currentSize) {
                return reply_server_error(F("WRITE FAILED"));
            }
        }
        DEBUG_PRINTLN(PSTR("Upload: WRITE, Bytes: ") + upload.currentSize);
    }
    else if (upload.status == UPLOAD_FILE_END) {
        if (upload_file) {
            upload_file.close();
        }
        DEBUG_PRINTLN(PSTR("Upload: END, Size: ") + upload.totalSize);
    }
}

/*
   Delete the file or folder designed by the given path.
   If it's a file, delete it.
   If it's a folder, delete all nested contents first then the folder itself

   IMPORTANT NOTE: using recursion is generally not recommended on embedded devices and can lead to crashes
   (stack overflow errors). It might crash in case of deeply nested filesystems.
   Please don't do this on a production system.
*/
void
delete_recursive(String const& path)
{
    File file  = SPIFFS.open(path, "r");
    bool isDir = file.isDirectory();
    file.close();

    // If it's a plain file, delete it
    if (!isDir) {
        SPIFFS.remove(path);
        return;
    }

    // Otherwise delete its contents first
    Dir dir = SPIFFS.openDir(path);
    while (dir.next()) {
        delete_recursive(path + '/' + dir.fileName());
    }

    // Then delete the folder itself
    SPIFFS.rmdir(path);
}

/*
   Handle a file deletion request
   Operation      | req.responseText
   ---------------+--------------------------------------------------------------
   Delete file    | parent of deleted file, or remaining ancestor
   Delete folder  | parent of deleted folder, or remaining ancestor
*/
void
handle_file_delete()
{
    if (web_server.args() == 0) {
        reply_server_error(F("BAD ARGS"));
        return;
    }
    String path{web_server.arg(0)};
    if (path.isEmpty() || path == "/") {
        return reply_bad_request(F("BAD PATH"));
    }

    DEBUG_PRINTLN(PSTR("handle_file_delete: ") + path);

    if (!SPIFFS.exists(path)) {
        return reply_not_found(FPSTR(FILE_NOT_FOUND));
    }
    delete_recursive(path);

    reply_ok_with_msg(last_existing_parent(path));
}

/*
   Handle the creation/rename of a new file
   Operation      | req.responseText
   ---------------+--------------------------------------------------------------
   Create file    | parent of created file
   Create folder  | parent of created folder
   Rename file    | parent of source file
   Move file      | parent of source file, or remaining ancestor
   Rename folder  | parent of source folder
   Move folder    | parent of source folder, or remaining ancestor
*/
void
handle_file_create()
{
    if (!web_server.hasArg(F("path"))) {
        reply_bad_request(F("PATH ARG MISSING"));
    }

    String path{web_server.arg(F("path"))};
    if (path.isEmpty()) {
        return reply_bad_request(F("PATH ARG MISSING"));
    }
    if (check_for_unsupported_path(path).length() > 0) {
        return reply_server_error(F("INVALID FILENAME"));
    }
    if (path == "/") {
        return reply_bad_request(F("BAD PATH"));
    }
    if (SPIFFS.exists(path)) {
        return reply_bad_request(F("FILE EXISTS"));
    }

    String src = web_server.arg(F("src"));
    if (src.isEmpty()) {
        // No source specified: creation
        DEBUG_PRINTLN(PSTR("handle_file_create: ") + path);
        if (path.endsWith("/")) {
            // Create a folder
            path.remove(path.length() - 1);
            if (!SPIFFS.mkdir(path)) {
                return reply_server_error(F("MKDIR FAILED"));
            }
        }
        else {
            // Create a file
            File file = SPIFFS.open(path, "w");
            if (file) {
                file.write((const char*)0);
                file.close();
            }
            else {
                return reply_server_error(F("CREATE FAILED"));
            }
        }
        if (path.lastIndexOf('/') > -1) {
            path = path.substring(0, path.lastIndexOf('/'));
        }
        reply_ok_with_msg(path);
    }
    else {
        // Source specified: rename
        if (src == "/") {
            return reply_bad_request(F("BAD SRC"));
        }
        if (!SPIFFS.exists(src)) {
            return reply_bad_request(F("SRC FILE NOT FOUND"));
        }

        DEBUG_PRINTLN(PSTR("handle_file_create: ") + path + PSTR(" from ") + src);

        if (path.endsWith("/")) {
            path.remove(path.length() - 1);
        }
        if (src.endsWith("/")) {
            src.remove(src.length() - 1);
        }
        if (!SPIFFS.rename(src, path)) {
            return reply_server_error(F("RENAME FAILED"));
        }
        reply_ok_with_msg(last_existing_parent(src));
    }
}

void
handle_file_list()
{
    if (!web_server.hasArg(F("dir"))) {
        reply_bad_request(F("DIR ARG MISSING"));
    }
    String path{web_server.arg(F("dir"))};
    if (path != "/" && !SPIFFS.exists(path)) {
        return reply_bad_request(F("BAD PATH"));
    }

    DEBUG_PRINTLN(PSTR("handle_file_list: ") + path);
    Dir dir = SPIFFS.openDir(path);

    // Use HTTP/1.1 Chunked response to avoid building a huge temporary string
    if (!web_server.chunkedResponseModeStart(200, FPSTR(TEXT_JSON))) {
        web_server.send(505, F("text/html"), F("HTTP1.1 required"));
        return;
    }

    // Use the same string for every line
    String                                            output;
    String                                            name;
    std::map<String, String, decltype(filename_less)> files_data(filename_less);
    output.reserve(64);
    while (dir.next()) {
        String error{check_for_unsupported_path(dir.fileName())};
        if (error.length() > 0) {
            DEBUG_PRINTLN(PSTR("Ignoring ") + error + dir.fileName());
            continue;
        }

        output += F("{\"type\":\"");
        if (dir.isDirectory()) {
            // There is no directories on SPIFFS. This code is here for compatibility with another (possible)
            // filesystems
            output += PSTR("dir");
        }
        else {
            output += F("file\",\"size\":\"");
            output += dir.fileSize();
        }

        output += F("\",\"name\":\"");
        // Always return names without leading "/"
        if (dir.fileName()[0] == '/') {
            output += &(dir.fileName()[1]);
            name = dir.fileName();  // name in map should contain leading "/"
        }
        else {
            output += dir.fileName();
            name = '/' + dir.fileName();  // name in map should contain leading "/"
        }

        output += "\"}";

        files_data.insert(std::make_pair(name, output));
        output.clear();
    }

    // Sort file list before building output.
    // NOTE: SPIFFS doesn't support directories! Path <directory_name/filename> can exist, but directory, as entoty,
    // not. Directories are used just as part of path.
    output = '[';
    for (auto const& data_pair : files_data) {
        output += data_pair.second + ',';
    }
    output.remove(output.length() - 1);
    output += ']';

    web_server.sendContent(output);
    web_server.chunkedResponseFinalize();
}

void
process_websocket_command(String const& command, uint8_t client_num)
{
    if (command == F("start_reading_logs")) {
        debugger_client_number    = client_num;
        debugger_client_connected = true;
        send_debug_logs();
    }
    else if (command == F("stop_reading_logs")) {
        send_debug_logs();
        debugger_client_connected = false;
    }
}

void
send_debug_logs()
{
    if (debugger_client_connected && (buffered_logger.get_log().length() > 0)) {
        web_socket.sendTXT(debugger_client_number, buffered_logger.get_log());
        buffered_logger.clear();
    }
}
