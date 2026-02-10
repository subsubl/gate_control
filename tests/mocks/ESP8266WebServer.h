#pragma once
#include "Arduino.h"
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTP_DELETE 2

typedef void (*HandlerFunction)();

class ESP8266WebServer {
public:
    ESP8266WebServer(int port) {}
    void on(const char* uri, int method, HandlerFunction fn) {}
    template<typename T>
    void on(const char* uri, int method, T fn) {} // For lambda
    void onNotFound(HandlerFunction fn) {}
    template<typename T>
    void onNotFound(T fn) {} // For lambda
    void begin() {}
    void stop() {}
    void handleClient() {}
    bool hasArg(const char* arg) { return true; }
    String arg(const char* arg) { return ""; }

    void send(int code, const char* type, const char* content) {}
    void send(int code, const char* type, const String& content) {}

    template<typename T>
    void streamFile(T f, const char* type) {}

    template<typename T>
    void streamFile(T f, const String& type) {}

    String uri() { return "/"; }
};
