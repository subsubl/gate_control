#ifdef ARDUINO
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <time.h>

#else
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#endif
#include <string.h>

#include "data_manager.h"
#include "logging_macros.h"
#include "mqtt_manager.h"
#include "web_server.h"

#define EXAMPLE_ESP_WIFI_SSID "Kader"
#define EXAMPLE_ESP_WIFI_PASS "kaderkodeljevo"
#define EXAMPLE_ESP_WIFI_MAXIMUM_RETRY 5

#if defined(ESP8266)
#define GPIO_RELAY_1 4  // D2 (GPIO 4)
#define GPIO_RELAY_2 14 // D5
#else
#define GPIO_RELAY_1 2  // D2 (GPIO 2)
#define GPIO_RELAY_2 18 // D5 (GPIO 18)
#endif

static const char *TAG = "GATE_CONTROL";

#ifndef ARDUINO
static int s_retry_num = 0;
void start_softap(void);

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < EXAMPLE_ESP_WIFI_MAXIMUM_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "retry to connect to the AP");
    } else {
      ESP_LOGI(TAG, "connect to the AP fail, falling back to SoftAP");
      start_softap();
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    // s_retry_num = 0;
  }
}
#endif

// Helper to print detailed WiFi status
void print_wifi_status() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n--- WiFi Connected ---");
    Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Subnet Mask: %s\n", WiFi.subnetMask().toString().c_str());
    Serial.printf("Gateway IP: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("MAC Address: %s\n", WiFi.macAddress().c_str());
    Serial.printf("BSSID: %s\n", WiFi.BSSIDstr().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
    Serial.println("----------------------\n");
  } else {
    Serial.println("\n--- WiFi Disconnected ---");
  }
}

void wifi_init_sta(void) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
  ESP_LOGI(TAG, "WiFi mode set to STA, attempting to connect to %s",
           EXAMPLE_ESP_WIFI_SSID);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    ESP_LOGI(TAG, "Connecting to WiFi... Status: %d", WiFi.status());
  }

  if (WiFi.status() == WL_CONNECTED) {
    print_wifi_status();
  }
}

#ifndef ARDUINO
#include "esp_sntp.h"
#endif

void init_sntp(void) {
#ifdef ARDUINO
  Serial.println("Initializing SNTP");
  configTime(0, 0, "pool.ntp.org");
#else
  ESP_LOGI(TAG, "Initializing SNTP");
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();
#endif

  // Set timezone to CET/CEST (Central Europe)
  // Rule: CET-1CEST,M3.5.0,M10.5.0/3
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
}


#ifndef ARDUINO
void init_spiffs(void) {
  ESP_LOGI(TAG, "Initializing SPIFFS");

  esp_vfs_spiffs_conf_t conf = {.base_path = "/spiffs",
                                .partition_label = NULL,
                                .max_files = 5,
                                .format_if_mount_failed = true};

  esp_err_t ret = esp_vfs_spiffs_register(&conf);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to mount or format filesystem");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE(TAG, "Failed to find SPIFFS partition");
    } else {
      ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
    }
    return;
  }

  size_t total = 0, used = 0;
  ret = esp_spiffs_info(NULL, &total, &used);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)",
             esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
  }
}
#endif

void trigger_relay(void) {
  ESP_LOGI(TAG, "Triggering Relays on GPIO %d & %d", GPIO_RELAY_1,
           GPIO_RELAY_2);
  digitalWrite(GPIO_RELAY_1, LOW);
  digitalWrite(GPIO_RELAY_2, LOW);
  delay(2000); // 2 second pulse
  digitalWrite(GPIO_RELAY_1, HIGH);
  digitalWrite(GPIO_RELAY_2, HIGH);
}

void setup(void) {
  Serial.begin(115200);
  // Initialize GPIO
  pinMode(GPIO_RELAY_1, OUTPUT);
  digitalWrite(GPIO_RELAY_1, HIGH);

  pinMode(GPIO_RELAY_2, OUTPUT);
  digitalWrite(GPIO_RELAY_2, HIGH);

  // Initialize SPIFFS
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed");
  } else {
    Serial.println("LittleFS Mounted");
  }

  // Initialize Data Manager
  data_manager_init();

  // Initialize WiFi
  ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
  wifi_init_sta();

  // Initialize SNTP
  init_sntp();

  // Start Web Server
  start_web_server();

  // Initialize MQTT
  mqtt_manager_init();
}

void loop() {
  static unsigned long last_debug_time = 0;
  if (millis() - last_debug_time > 5000) {
    last_debug_time = millis();

    long rssi = WiFi.RSSI();
    String ip = WiFi.localIP().toString();
    uint32_t free_heap = ESP.getFreeHeap();

    Serial.printf(
        "[DEBUG] Uptime: %lu ms | WiFi: %s | IP: %s | Signal: %ld dBm | Free "
        "Heap: %u bytes\n",
        millis(),
        (WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected"),
        ip.c_str(), rssi, free_heap);

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(
          "[WARN] WiFi lost! Attempting reconnect in loop if needed...");
      // Logic to reconnect if needed could go here, though ESP8266/ESP32
      // usually auto-reconnects
    }
  }

  // Handle Web Server Client
  web_server_loop();
}
