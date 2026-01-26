#ifndef LOGGING_MACROS_H
#define LOGGING_MACROS_H

#ifdef ARDUINO
#include <Arduino.h>
// Macros to mimic ESP-IDF logging in Arduino
// Uses TAG and adds newline to match IDF behavior
#define ESP_LOGI(tag, ...)                                                     \
  do {                                                                         \
    Serial.printf("[%s] ", tag);                                               \
    Serial.printf(__VA_ARGS__);                                                \
    Serial.println();                                                          \
  } while (0)
#define ESP_LOGE(tag, ...)                                                     \
  do {                                                                         \
    Serial.printf("[%s] ERROR: ", tag);                                        \
    Serial.printf(__VA_ARGS__);                                                \
    Serial.println();                                                          \
  } while (0)
#define ESP_LOGW(tag, ...)                                                     \
  do {                                                                         \
    Serial.printf("[%s] WARN: ", tag);                                         \
    Serial.printf(__VA_ARGS__);                                                \
    Serial.println();                                                          \
  } while (0)

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERROR_CHECK(x)                                                     \
  do {                                                                         \
    if ((x) != ESP_OK) {                                                       \
      Serial.printf("Error at %s:%d\n", __FILE__, __LINE__);                   \
    }                                                                          \
  } while (0)
#endif

#endif // LOGGING_MACROS_H
