#pragma once
#include "Arduino.h"

struct JsonVariant {
    JsonVariant() {}
    operator const char*() const { return ""; }
    operator int() const { return 0; }
    operator uint16_t() const { return 0; }
    operator uint8_t() const { return 0; }

    template<typename T>
    void operator=(T val) {}

    bool isNull() const { return false; }
};

struct JsonObject {
    JsonVariant operator[](const char* key) { return JsonVariant(); }
};

struct JsonArray {
    template<typename T> T add() { return T(); }
};

struct JsonDocument {
    JsonVariant operator[](const char* key) { return JsonVariant(); }
    template<typename T> T to() { return T(); }
};

enum DeserializationError { Ok, Error };
DeserializationError deserializeJson(JsonDocument& doc, String input) { return Ok; }
void serializeJson(JsonDocument& doc, String& output) {}
