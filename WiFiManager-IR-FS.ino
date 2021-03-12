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
replyOK()
{
    web_server.send(200, FPSTR(TEXT_PLAIN), "");
}

void
replyOKWithMsg(String const& msg)
{
    web_server.send(200, FPSTR(TEXT_PLAIN), msg);
}

void
replyOKJsonWithMsg(String const& msg)
{
    web_server.send(200, FPSTR(TEXT_JSON), msg);
}

void
replyNotFound(String const& msg)
{
    web_server.send(404, FPSTR(TEXT_PLAIN), msg);
}

void
replyBadRequest(String const& msg)
{
    Serial.println(msg);
    web_server.send(400, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

void
replyServerError(String const& msg)
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
            replyNotFound(F("FileNotFound"));
        }
    });
    // Create file
    web_server.on(F("/edit"), HTTP_PUT, handleFileCreate);
    //Удаление файла
    web_server.on(F("/edit"), HTTP_DELETE, handleFileDelete);
    // first callback is called after the request has ended with all parsed arguments
    // second callback handles file uploads at that location
    web_server.on(
        F("/edit"),
        HTTP_POST,
        []() {
            Serial.println("LAMBIN /edit HTTP_POST");
            replyOK();
        },
        handle_file_upload);

    // called when the url is not defined here
    // use it to load content from SPIFFS
    web_server.onNotFound([]() {
        if (!handle_file_read(web_server.uri())) {
            replyNotFound(F("FileNotFound"));
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
            replyOKWithMsg(Update.hasError() ? F("FAIL") : F("OK"));
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
        // DBG_OUTPUT_PORT.print("handle_file_upload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
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

void
handleFileDelete()
{
    Serial.println("LAMBIN handleFileDelete()");

    if (web_server.args() == 0) {
        replyServerError(F("BAD ARGS"));
        return;
    }
    String path = web_server.arg(0);

    if (path == "/") {
        replyServerError(F("BAD PATH"));
        return;
    }
    if (!SPIFFS.exists(path)) {
        replyNotFound(F("FileNotFound"));
        return;
    }
    SPIFFS.remove(path);
    replyOK();
    path = String();
}

void
handleFileCreate()
{
    Serial.println("LAMBIN handleFileCreate()");

    if (web_server.args() == 0) {
        replyServerError(F("BAD ARGS"));
        return;
    }
    String path = web_server.arg(0);

    if (path == "/") {
        replyServerError(F("BAD PATH"));
        return;
    }
    if (SPIFFS.exists(path)) {
        replyServerError(F("FILE EXISTS"));
        return;
    }
    File file = SPIFFS.open(path, "w");
    if (file) {
        file.close();
    }
    else {
        replyServerError(F("CREATE FAILED"));
        return;
    }
    replyOK();
    path = String();
}

void
handle_file_list()
{
    if (!web_server.hasArg(F("dir"))) {
        replyBadRequest(F("DIR ARG MISSING"));
    }
    String path{web_server.arg("dir")};
    if (path != "/" && !SPIFFS.exists(path)) {
        return replyBadRequest("BAD PATH");
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
