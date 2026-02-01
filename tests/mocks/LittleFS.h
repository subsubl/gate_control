#pragma once
#include "Arduino.h"

class File {
public:
    void close() {}
};

class LittleFSClass {
public:
    bool exists(String path) { return true; }
    File open(String path, const char* mode) { return File(); }
};

static LittleFSClass LittleFS;
