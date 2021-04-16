#include "WebServer.h"

#include <cctype>
#include <map>

#include <ESP8266SSDP.h>
#include <WString.h>

#include "logger.h"

namespace
{
static const char TEXT_PLAIN[] PROGMEM     = "text/plain";
static const char TEXT_JSON[] PROGMEM      = "text/json";
static const char FS_INIT_ERROR[] PROGMEM  = "FS INIT ERROR";
static const char FILE_NOT_FOUND[] PROGMEM = "FileNotFound";

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

WebServer::WebServer()
  : web_server_{port_}
  , handlers_{
        nullptr,
    }
{
}

void
WebServer::init()
{
    // SSDP description
    web_server_.on(F("/description.xml"), HTTP_GET, [this]() { SSDP.schema(web_server_.client()); });

    // HTTP pages to work with file system
    // List directory
    web_server_.on(F("/list"), HTTP_GET, [this]() { handle_file_list(); });
    // Load editor
    web_server_.on(F("/edit"), HTTP_GET, [this]() {
        if (!handle_file_read(F("/edit.htm"))) {
            reply_not_found(FPSTR(FILE_NOT_FOUND));
        }
    });
    // Create file
    web_server_.on(F("/edit"), HTTP_PUT, [this]() { handle_file_create(); });
    // Delete file
    web_server_.on(F("/edit"), HTTP_DELETE, [this]() { handle_file_delete(); });

    // Upload file
    // - first callback is called after the request has ended with all parsed arguments
    // - second callback handles file upload at that location
    web_server_.on(
        F("/edit"), HTTP_POST, [this]() { reply_ok(); }, [this]() { handle_file_upload(); });

    // Called when the url is not defined here
    // Use it to load content from SPIFFS
    web_server_.onNotFound([this]() {
        if (!handle_file_read(web_server_.uri())) {
            reply_not_found(FPSTR(FILE_NOT_FOUND));
        }
    });

    // HTTP pages for Settings
    // Update ESP firmware
    web_server_.on(
        F("/update"),
        HTTP_POST,
        [this]() {
            // This code is from example. Not sure if we really need it
            //
            // web_server_.sendHeader(F("Connection"), F("close"));
            // web_server_.sendHeader(F("Access-Control-Allow-Origin"), "*");
            // reply_ok_with_msg(Update.hasError() ? F("FAIL") : F("ESP firmware update completed! Rebooting..."));

            // If there were errors during uploading of file, this is the only place, where we can send message to
            // client about it
            if (esp_firmware_upload_error_.length() != 0) {
                DEBUG_PRINTLN(PSTR("Sending error to WebUI: ") + esp_firmware_upload_error_);
                reply_server_error(esp_firmware_upload_error_);
                esp_firmware_upload_error_ = "";
            }
            else {
                reply_ok_with_msg(F("ESP firmware update completed! Rebooting..."));
                DEBUG_PRINTLN(F("Rebooting..."));
                handle_reboot_esp();  // Schedule reboot
            }
        },
        [this]() { handle_esp_sw_upload(); });

    web_server_.on(F("/reset_wifi_settings"), HTTP_POST, [this]() { handle_reset_wifi_settings(); });
    web_server_.on(F("/reboot_esp"), HTTP_POST, [this]() {
        reply_ok();
        handle_reboot_esp();
    });

    web_server_.begin();
    DEBUG_PRINTLN(F("Web server initialized"));
}

void
WebServer::loop()
{
    web_server_.handleClient();
}

void
WebServer::set_handler(Event event, EventHandler handler)
{
    handlers_[static_cast<size_t>(event)] = handler;
}

void
WebServer::reply_ok()
{
    web_server_.send(200, FPSTR(TEXT_PLAIN), "");
}

void
WebServer::reply_ok_with_msg(String const& msg)
{
    web_server_.send(200, FPSTR(TEXT_PLAIN), msg);
}

void
WebServer::reply_ok_json_with_msg(String const& msg)
{
    web_server_.send(200, FPSTR(TEXT_JSON), msg);
}

void
WebServer::reply_not_found(String const& msg)
{
    web_server_.send(404, FPSTR(TEXT_PLAIN), msg);
}

void
WebServer::reply_bad_request(String const& msg)
{
    DEBUG_PRINTLN(msg);
    web_server_.send(400, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

void
WebServer::reply_server_error(String const& msg)
{
    DEBUG_PRINTLN(msg);
    web_server_.send(500, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

void
WebServer::handle_file_list()
{
    if (!web_server_.hasArg(F("dir"))) {
        reply_bad_request(F("DIR ARG MISSING"));
    }
    String path{web_server_.arg(F("dir"))};
    if (path != "/" && !SPIFFS.exists(path)) {
        return reply_bad_request(F("BAD PATH"));
    }

    DEBUG_PRINTLN(PSTR("handle_file_list: ") + path);
    Dir dir = SPIFFS.openDir(path);

    // Use HTTP/1.1 Chunked response to avoid building a huge temporary string
    if (!web_server_.chunkedResponseModeStart(200, FPSTR(TEXT_JSON))) {
        web_server_.send(505, F("text/html"), F("HTTP1.1 required"));
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

    web_server_.sendContent(output);
    web_server_.chunkedResponseFinalize();
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
WebServer::handle_file_create()
{
    if (!web_server_.hasArg(F("path"))) {
        reply_bad_request(F("PATH ARG MISSING"));
    }

    String path{web_server_.arg(F("path"))};
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

    String src = web_server_.arg(F("src"));
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


/*
   Delete the file or folder designed by the given path.
   If it's a file, delete it.
   If it's a folder, delete all nested contents first then the folder itself

   IMPORTANT NOTE: using recursion is generally not recommended on embedded devices and can lead to crashes
   (stack overflow errors). It might crash in case of deeply nested filesystems.
   Please don't do this on a production system.
*/
void
WebServer::delete_recursive(String const& path)
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
WebServer::handle_file_delete()
{
    if (web_server_.args() == 0) {
        reply_server_error(F("BAD ARGS"));
        return;
    }
    String path{web_server_.arg(0)};
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

void
WebServer::handle_file_upload()
{
    if (web_server_.uri() != F("/edit")) {
        return;
    }

    HTTPUpload& upload = web_server_.upload();
    if (upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        // Make sure paths always start with "/"
        if (!filename.startsWith("/")) {
            filename = "/" + filename;
        }
        DEBUG_PRINTLN(PSTR("handle_file_upload Name: ") + filename);
        upload_file_ = SPIFFS.open(filename, "w");
        if (!upload_file_) {
            return reply_server_error(F("CREATE FAILED"));
        }
        DEBUG_PRINTLN(PSTR("Upload: START, filename: ") + filename);
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
        if (upload_file_) {
            size_t bytesWritten = upload_file_.write(upload.buf, upload.currentSize);
            if (bytesWritten != upload.currentSize) {
                return reply_server_error(F("WRITE FAILED"));
            }
        }
        DEBUG_PRINTLN(PSTR("Upload: WRITE, Bytes: ") + String(upload.currentSize));
    }
    else if (upload.status == UPLOAD_FILE_END) {
        if (upload_file_) {
            upload_file_.close();
        }
        DEBUG_PRINTLN(PSTR("Upload: END, Size: ") + String(upload.totalSize));
    }
}

bool
WebServer::handle_file_read(String path)
{
    DEBUG_PRINTLN(PSTR("handle_file_read: ") + path);

    if (path.endsWith("/")) {
        path += F("index.htm");
    }

    String contentType;
    if (web_server_.hasArg(F("download"))) {
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
        if (web_server_.streamFile(file, contentType) != file.size()) {
            DEBUG_PRINTLN(F("Sent less data than expected!"));
        }
        file.close();
        return true;
    }

    return false;
}

void
WebServer::handle_esp_sw_upload()
{
    // NOTE! There is no way in HTML to stop client from uploading file. So, if it starts uploading of too big file and
    // ESP, as a server, identifies it, the only thing ESP can do is to mark this uploaded file as invalid, receive it
    // completely and only then send error. But error should be sent only once, otherwise when client will finish
    // uploading it will receive all error messages, sent during upload, at one time

    if (web_server_.args() < 1) {
        esp_firmware_upload_error_ = F("ERROR: file size is not provided!");
        return;
    }

    HTTPUpload& upload = web_server_.upload();
    if (upload.status == UPLOAD_FILE_START) {
        auto file_size = web_server_.arg(F("file_size")).toInt();
        if (!Update.begin(file_size)) {
            Update.end();
            Update.printError(DGB_STREAM);
            esp_firmware_upload_error_ =
                PSTR("ERROR: not enough space! Available: ") + String(ESP.getFreeSketchSpace());
            return;
        }

        DGB_STREAM.setDebugOutput(true);
        WiFiUDP::stopAll();
        DEBUG_PRINTLN(PSTR("Start uploading file: ") + upload.filename);
        esp_firmware_upload_error_ = "";
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
        if (esp_firmware_upload_error_.length() != 0) {
            // Ignore uploading of file which is already marked as invalid. Do NOT send any errors to client here,
            // because there is no way to stop uploading but all sent messages will come together to client when it
            // finish uploading
            return;
        }

        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.end();
            Update.printError(DGB_STREAM);
            esp_firmware_upload_error_ = PSTR("ERROR: can not write file! Update error ") + String(Update.getError());
            DGB_STREAM.setDebugOutput(false);
            return;
        }
    }
    else if (upload.status == UPLOAD_FILE_END) {
        if (esp_firmware_upload_error_.length() != 0) {
            // Ignore finalizing of upload of file which is already marked as invalid. Do NOT send any errors to client
            // here, because there is no way to stop uploading but all sent messages will come together to client when
            // it finish uploading
            return;
        }

        if (Update.end(true)) {  // true to set the size to the current progress
            DEBUG_PRINTLN(PSTR("Update completed. Uploaded file size: ") + String(upload.totalSize));
        }
        else {
            Update.end();
            Update.printError(DGB_STREAM);
            esp_firmware_upload_error_ =
                PSTR("ERROR: can not finalize file! Update error ") + String(Update.getError());
        }
        DGB_STREAM.setDebugOutput(false);
    }
    else {
        Update.end();
        Update.printError(DGB_STREAM);
        esp_firmware_upload_error_ = F("ERROR: uploading of file was aborted");
        DGB_STREAM.setDebugOutput(false);
        return;
    }
    yield();
}

void
WebServer::handle_reset_wifi_settings()
{
    reply_ok();
    delay(500);  // Add delay to make sure response is sent to client

    DEBUG_PRINTLN(F("handle_reset_wifi_settings"));
    if (handlers_[static_cast<size_t>(Event::RESET_WIFI_SETTINGS)] != nullptr) {
        handlers_[static_cast<size_t>(Event::RESET_WIFI_SETTINGS)]("");
    }
}

void
WebServer::handle_reboot_esp()
{
    DEBUG_PRINTLN(F("handle_reboot_esp"));
    if (handlers_[static_cast<size_t>(Event::REBOOT_ESP)] != nullptr) {
        handlers_[static_cast<size_t>(Event::REBOOT_ESP)]("");
    }
}
