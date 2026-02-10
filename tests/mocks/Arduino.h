#pragma once
#include <cstring>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdint.h>

#define ARDUINO 100

class String {
public:
    std::string s;
    String(const char* str = "") : s(str ? str : "") {}
    String(const std::string& str) : s(str) {}
    String operator+(const char* other) const { return String(s + other); }
    String operator+(const String& other) const { return String(s + other.s); }
    String& operator+=(const char* other) { s += other; return *this; }
    String& operator+=(const String& other) { s += other.s; return *this; }
    bool endsWith(const char* suffix) const {
        if (s.length() >= strlen(suffix)) {
            return s.compare(s.length() - strlen(suffix), strlen(suffix), suffix) == 0;
        }
        return false;
    }
    bool endsWith(const String& suffix) const {
        return endsWith(suffix.s.c_str());
    }
    bool operator==(const char* other) const { return s == other; }
    bool operator==(const String& other) const { return s == other.s; }
    const char* c_str() const { return s.c_str(); }
    int length() const { return s.length(); }
};

class SerialClass {
public:
    void printf(const char* fmt, ...) {}
    void println(const char* s = "") {}
};
static SerialClass Serial;

#define ESP_LOGI(tag, fmt, ...) printf(fmt, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf(fmt, ##__VA_ARGS__)
