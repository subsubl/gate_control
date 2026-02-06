#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <string>

// Mock String class simulating Arduino String (WString) with heap allocation
class String {
public:
    char* buffer;
    size_t len;
    static int copyCount;
    static int mallocCount;

    void init(const char* str) {
        if (str && str[0] != '\0') {
            len = strlen(str);
            buffer = new char[len + 1];
            mallocCount++;
            strcpy(buffer, str);
        } else {
            len = 0;
            buffer = nullptr;
        }
    }

    String() : buffer(nullptr), len(0) { }
    String(const char* str) : buffer(nullptr), len(0) { init(str); }
    String(const std::string& str) : buffer(nullptr), len(0) { init(str.c_str()); }

    // Copy constructor - Always allocates
    String(const String& other) : buffer(nullptr), len(0) {
        copyCount++;
        init(other.buffer);
    }

    // Destructor
    ~String() {
        delete[] buffer;
    }

    // Assignment operator
    String& operator=(const String& other) {
        if (this != &other) {
            delete[] buffer;
            init(other.buffer);
        }
        return *this;
    }

    // endsWith
    bool endsWith(const String& suffix) const {
        if (len >= suffix.len) {
            const char* s_ptr = buffer ? buffer : "";
            const char* suf_ptr = suffix.buffer ? suffix.buffer : "";
            if (len == 0 && suffix.len == 0) return true;
            if (len < suffix.len) return false;
            return (strcmp(s_ptr + len - suffix.len, suf_ptr) == 0);
        }
        return false;
    }

    bool endsWith(const char* suffix) const {
        return endsWith(String(suffix));
    }

    // operator==
    bool operator==(const char* other) const {
        const char* my_buf = buffer ? buffer : "";
        const char* other_buf = other ? other : "";
        return strcmp(my_buf, other_buf) == 0;
    }

    // operator+
    String operator+(const char* other) const {
        String ret;
        size_t otherLen = other ? strlen(other) : 0;
        ret.len = len + otherLen;
        if (ret.len > 0) {
            ret.buffer = new char[ret.len + 1];
            mallocCount++;
            if (buffer) strcpy(ret.buffer, buffer);
            else ret.buffer[0] = '\0';

            if (other) strcat(ret.buffer, other);
        }
        return ret;
    }

    // operator+=
    String& operator+=(const char* other) {
        if (!other || !*other) return *this;
        size_t otherLen = strlen(other);
        char* newBuffer = new char[len + otherLen + 1];
        mallocCount++;
        if (buffer) strcpy(newBuffer, buffer);
        else newBuffer[0] = '\0';

        strcat(newBuffer, other);
        delete[] buffer;
        buffer = newBuffer;
        len += otherLen;
        return *this;
    }
};

int String::copyCount = 0;
int String::mallocCount = 0;

// Original function (simulated)
bool handleFileRead_Value(String path) {
    if (path.endsWith("/"))
        path += "index.html";
    if (path == "/admin")
        path += ".html";

    // Logic matching handleFileReadLogic to be fair
    String contentType = "text/plain";
    if (path.endsWith(".html")) contentType = "text/html";

    volatile size_t l = path.len;
    return true;
}

// Handler: Static Files Logic
bool handleFileReadLogic(const String& path) {
    String contentType = "text/plain";
    if (path.endsWith(".html")) contentType = "text/html";
    volatile size_t l = path.len;
    return true;
}

// Handler: Static Files Wrapper (Optimization: avoid copy unless needed)
bool handleFileRead_Ref(const String& path) {
  if (path.endsWith("/"))
    return handleFileReadLogic(path + "index.html");
  if (path == "/admin")
    return handleFileReadLogic(path + ".html");
  return handleFileReadLogic(path);
}

int main() {
    const int ITERATIONS = 1000000;
    std::string padding = "____________________________________";

    std::vector<std::string> testPaths = {
        "/style.css" + padding,
        "/script.js" + padding,
        "/long/path/ending/in/", // Triggers endsWith("/")
        "/admin" // Triggers == "/admin" (short, but allocates)
    };

    std::cout << "Running benchmark with " << ITERATIONS << " iterations over " << testPaths.size() << " paths." << std::endl;

    // 1. Benchmark Pass-by-Value
    String::copyCount = 0;
    String::mallocCount = 0;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        for (const auto& p : testPaths) {
            String temp(p.c_str());
            // Reset malloc count for temp creation to focus on function overhead?
            // No, we want total system allocs.
            handleFileRead_Value(temp);
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    long durationValue = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    int copiesValue = String::copyCount;
    int mallocsValue = String::mallocCount;

    std::cout << "Pass-by-Value:" << std::endl;
    std::cout << "  Time: " << durationValue << " ms" << std::endl;
    std::cout << "  Copies: " << copiesValue << std::endl;
    std::cout << "  Mallocs: " << mallocsValue << std::endl;

    // 2. Benchmark Pass-by-Reference
    String::copyCount = 0;
    String::mallocCount = 0;
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERATIONS; ++i) {
        for (const auto& p : testPaths) {
             String temp(p.c_str());
            handleFileRead_Ref(temp);
        }
    }
    end = std::chrono::high_resolution_clock::now();
    long durationRef = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    int copiesRef = String::copyCount;
    int mallocsRef = String::mallocCount;

    std::cout << "Pass-by-Reference:" << std::endl;
    std::cout << "  Time: " << durationRef << " ms" << std::endl;
    std::cout << "  Copies: " << copiesRef << std::endl;
    std::cout << "  Mallocs: " << mallocsRef << std::endl;

    std::cout << "Allocations saved: " << (long)mallocsValue - mallocsRef << std::endl;

    return 0;
}
