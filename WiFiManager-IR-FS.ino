#include <ESP8266SSDP.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <WiFiManager.h>

namespace
{
ESP8266WebServer web_server(80);

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
    Serial.println(msg);
    web_server.send(400, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

void
reply_server_error(String const& msg)
{
    Serial.println(msg);
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
    Serial.println(String("Last existing parent: ") + path);
    return path;
}

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
    web_server_init();
    SSDP_init();

    // TODO: think about replacing all Web file operations with FTP server
}

void
loop()
{
    // Just debug. Remove it in final version
    Serial.print("+");
    delay(1000);

    web_server.handleClient();
}

void
configModeCallback(WiFiManager* wifiManager)
{
    Serial.print(F("Could not connect to Access Point. Entering config mode. Connect to \""));
    Serial.print(wifiManager->getConfigPortalSSID());
    Serial.print(F("\" WiFi network and open http://"));
    Serial.print(WiFi.softAPIP());
    Serial.println(F(" (if it didn't happen automatically) to configure WiFi settings of SAD-lamp"));
}

void
init_wifi()
{
    // WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    // For debug info
    // Serial.setDebugOutput(true);
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
        Serial.println(F("failed to connect and hit timeout"));
        delay(3000);
        // if we still have not connected restart and try all over again
        ESP.restart();
        delay(5000);
    }

    Serial.print(F("Connected to Access Point \""));
    Serial.print(WiFi.SSID());
    Serial.print(F("\". IP: "));
    Serial.print(WiFi.localIP().toString());
    Serial.print(F("; mask: "));
    Serial.print(WiFi.subnetMask().toString());
    Serial.print(F("; gateway: "));
    Serial.print(WiFi.gatewayIP().toString());
    Serial.println();
}

void
SPIFFS_init()
{
}

void
SSDP_init()
{
    SSDP.setDeviceType("upnp:rootdevice");
    SSDP.setSchemaURL("description.xml");
    SSDP.setHTTPPort(80);
    SSDP.setName("SAD-Lamp");
    SSDP.setSerialNumber("1");
    SSDP.setURL("/");
    SSDP.setModelName("SAD-Lamp");
    SSDP.setModelNumber("1");
    // SSDP.setModelURL("http://adress.ru/page/");
    SSDP.setManufacturer("Lambin Alexey");
    // SSDP.setManufacturerURL("http://www.address.ru");
    SSDP.begin();

    Serial.println("SSDP initialized");
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
    //Удаление файла
    web_server.on(F("/edit"), HTTP_DELETE, handle_file_delete);
    // first callback is called after the request has ended with all parsed arguments
    // second callback handles file uploads at that location
    web_server.on(
        F("/edit"),
        HTTP_POST,
        []() {
            Serial.println("LAMBIN /edit HTTP_POST");
            reply_ok();
        },
        handle_file_upload);

    // called when the url is not defined here
    // use it to load content from SPIFFS
    web_server.onNotFound([]() {
        if (!handle_file_read(web_server.uri())) {
            reply_not_found(FPSTR(FILE_NOT_FOUND));
        }
    });

    //Обнавление
    web_server.on(
        F("/update"),
        HTTP_POST,
        []() {
            Serial.println("LAMBIN /update HTTP_POST 1");

            web_server.sendHeader(F("Connection"), F("close"));
            web_server.sendHeader(F("Access-Control-Allow-Origin"), "*");
            reply_ok_with_msg(Update.hasError() ? F("FAIL") : F("OK"));
            ESP.restart();
        },
        []() {
            Serial.println("LAMBIN /update HTTP_POST 2");

            HTTPUpload& upload = web_server.upload();
            if (upload.status == UPLOAD_FILE_START) {
                Serial.setDebugOutput(true);
                WiFiUDP::stopAll();
                Serial.printf_P(PSTR("Update: %s\n"), upload.filename.c_str());
                uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
                if (!Update.begin(maxSketchSpace)) {  // start with max available size
                    Update.printError(Serial);
                }
            }
            else if (upload.status == UPLOAD_FILE_WRITE) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Update.printError(Serial);
                }
            }
            else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {  // true to set the size to the current progress
                    Serial.printf_P(PSTR("Update Success: %u\nRebooting...\n"), upload.totalSize);
                }
                else {
                    Update.printError(Serial);
                }
                Serial.setDebugOutput(false);
            }
            yield();
        });

    web_server.begin();
}

bool
handle_file_read(String path)
{
    Serial.println("handle_file_read: " + path);

    if (path.endsWith("/")) {
        path += "index.htm";
    }

    String contentType;
    if (web_server.hasArg("download")) {
        contentType = F("application/octet-stream");
    }
    else {
        contentType = mime::getContentType(path);
    }

    if (!SPIFFS.exists(path)) {
        // File not found, try gzip version
        path = path + ".gz";
    }
    if (SPIFFS.exists(path)) {
        File file = SPIFFS.open(path, "r");
        if (web_server.streamFile(file, contentType) != file.size()) {
            Serial.println("Sent less data than expected!");
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

    File        fsUploadFile;
    HTTPUpload& upload = web_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        if (!filename.startsWith("/"))
            filename = "/" + filename;

        fsUploadFile = SPIFFS.open(filename, "w");
        filename     = String();
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
        Serial.print("handle_file_upload Data: ");
        Serial.println(upload.currentSize);
        if (fsUploadFile) {
            fsUploadFile.write(upload.buf, upload.currentSize);
        }
    }
    else if (upload.status == UPLOAD_FILE_END) {
        if (fsUploadFile) {
            fsUploadFile.close();
        }
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
        return reply_bad_request("BAD PATH");
    }

    Serial.println("handle_file_delete: " + path);

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

    String path{web_server.arg("path")};
    if (path.isEmpty()) {
        return reply_bad_request(F("PATH ARG MISSING"));
    }
    if (check_for_unsupported_path(path).length() > 0) {
        return reply_server_error(F("INVALID FILENAME"));
    }
    if (path == "/") {
        return reply_bad_request("BAD PATH");
    }
    if (SPIFFS.exists(path)) {
        return reply_bad_request(F("FILE EXISTS"));
    }

    String src = web_server.arg("src");
    if (src.isEmpty()) {
        // No source specified: creation
        Serial.println("handle_file_create: " + path);
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
            return reply_bad_request("BAD SRC");
        }
        if (!SPIFFS.exists(src)) {
            return reply_bad_request(F("SRC FILE NOT FOUND"));
        }

        Serial.println("handle_file_create: " + path + " from " + src);

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
    String path{web_server.arg("dir")};
    if (path != "/" && !SPIFFS.exists(path)) {
        return reply_bad_request("BAD PATH");
    }

    Serial.println("handle_file_list: " + path);
    Dir dir = SPIFFS.openDir(path);

    // Use HTTP/1.1 Chunked response to avoid building a huge temporary string
    if (!web_server.chunkedResponseModeStart(200, FPSTR(TEXT_JSON))) {
        web_server.send(505, F("text/html"), F("HTTP1.1 required"));
        return;
    }

    // Use the same string for every line
    String output;
    output.reserve(64);
    while (dir.next()) {
        String error{check_for_unsupported_path(dir.fileName())};
        if (error.length() > 0) {
            Serial.println("Ignoring " + error + dir.fileName());
            continue;
        }

        if (output.length()) {
            // Send string from previous iteration as an HTTP chunk
            web_server.sendContent(output);
            output = ',';
        }
        else {
            output = '[';
        }

        output += F("{\"type\":\"");
        if (dir.isDirectory()) {
            output += "dir";
        }
        else {
            output += F("file\",\"size\":\"");
            output += dir.fileSize();
        }

        output += F("\",\"name\":\"");
        // Always return names without leading "/"
        if (dir.fileName()[0] == '/') {
            output += &(dir.fileName()[1]);
        }
        else {
            output += dir.fileName();
        }

        output += "\"}";
    }

    // Send last string
    output += "]";
    web_server.sendContent(output);
    web_server.chunkedResponseFinalize();
}
