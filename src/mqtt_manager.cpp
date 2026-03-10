#include "mqtt_manager.h"
#ifdef ARDUINO
#include <PubSubClient.h>
#include <WiFiClient.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif
#else
#include "esp_log.h"
#include "mqtt_client.h"

#endif
#include "data_manager.h"      // For logging access if needed
#include "gate_control_main.h" // To trigger relay
#include "logging_macros.h"

static const char *TAG = "MQTT_MANAGER";
#ifdef ARDUINO
WiFiClient wifiClient;
PubSubClient client(wifiClient);
#else
static esp_mqtt_client_handle_t client = NULL;
#endif

#ifndef ARDUINO
#include "nvs.h"
#include "nvs_flash.h"

#endif

// Config Structure
typedef struct {
  char broker_uri[64];
  char topic_cmd[64];
  char topic_status[64];
} mqtt_config_t;

static mqtt_config_t mqtt_config;

#ifdef ARDUINO
String get_mqtt_client_id() {
  String id = "Gate-";
  id += WiFi.macAddress();
  return id;
}
#endif

void mqtt_load_config(void) {
#ifdef ARDUINO
  // Defaults
  strcpy(mqtt_config.broker_uri, "test.mosquitto.org");
  strcpy(mqtt_config.topic_cmd, "antigravity_gate/cmd");
  strcpy(mqtt_config.topic_status, "antigravity_gate/status");
  ESP_LOGW(TAG, "MQTT Config using defaults");
#else
  nvs_handle_t handle;
  esp_err_t err = nvs_open("mqtt_cfg", NVS_READONLY, &handle);
  if (err == ESP_OK) {
    size_t len = sizeof(mqtt_config_t);
    nvs_get_blob(handle, "config", &mqtt_config, &len);
    nvs_close(handle);
    ESP_LOGI(TAG, "MQTT Config loaded from NVS");
  } else {
    // Defaults
    strcpy(mqtt_config.broker_uri, "mqtt://test.mosquitto.org");
    strcpy(mqtt_config.topic_cmd, "antigravity_gate/cmd");
    strcpy(mqtt_config.topic_status, "antigravity_gate/status");
    ESP_LOGW(TAG, "MQTT Config not found, using defaults");
  }
#endif
}

void mqtt_save_config(const mqtt_config_t *new_config) {
#ifdef ARDUINO
  // Not implemented
  memcpy(&mqtt_config, new_config, sizeof(mqtt_config_t));
#else
  nvs_handle_t handle;
  ESP_ERROR_CHECK(nvs_open("mqtt_cfg", NVS_READWRITE, &handle));
  nvs_set_blob(handle, "config", new_config, sizeof(mqtt_config_t));
  nvs_commit(handle);
  nvs_close(handle);
  memcpy(&mqtt_config, new_config, sizeof(mqtt_config_t));
#endif
}

// Public API for Web Server
void mqtt_manager_get_config(char *uri, char *cmd, char *status) {
  strcpy(uri, mqtt_config.broker_uri);
  strcpy(cmd, mqtt_config.topic_cmd);
  strcpy(status, mqtt_config.topic_status);
}

void mqtt_manager_update_config(const char *uri, const char *cmd,
                                const char *status) {
  mqtt_config_t new_cfg;
  if (uri)
    strncpy(new_cfg.broker_uri, uri, 63);
  if (cmd)
    strncpy(new_cfg.topic_cmd, cmd, 63);
  if (status)
    strncpy(new_cfg.topic_status, status, 63);

  mqtt_save_config(&new_cfg);

  // Reconnect
#ifdef ARDUINO
  if (client.connected()) {
    client.disconnect();
  }
  client.setServer(new_cfg.broker_uri, 1883);
  if (client.connect(get_mqtt_client_id().c_str())) {
    client.subscribe(new_cfg.topic_cmd);
    client.publish(new_cfg.topic_status, "ONLINE");
  }
#else
  if (client) {
    esp_mqtt_client_stop(client);
    esp_mqtt_client_set_uri(client, new_cfg.broker_uri);
    esp_mqtt_client_start(client);
  }
#endif
}

#ifndef ARDUINO
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = event_data;

  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT Connected");
    esp_mqtt_client_subscribe(client, mqtt_config.topic_cmd, 0);
    esp_mqtt_client_publish(client, mqtt_config.topic_status, "ONLINE", 0, 1,
                            0);
    break;

  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "MQTT Disconnected");
    break;

  case MQTT_EVENT_DATA:
    ESP_LOGI(TAG, "MQTT Data received");
    printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
    printf("DATA=%.*s\r\n", event->data_len, event->data);

    // Handle Command
    if (strncmp(event->topic, mqtt_config.topic_cmd, event->topic_len) == 0) {
      if (strncmp(event->data, "OPEN", event->data_len) == 0) {
        ESP_LOGI(TAG, "Received OPEN command via MQTT");
        trigger_relay(); // Defined in main
        mqtt_manager_publish_status("OPENING");
        data_manager_log_access("MQTT", true, "Remote Open");
      }
    }
    break;

  default:
    break;
  }
}
#endif

#ifdef ARDUINO
void mqtt_callback(char *topic, byte *payload, unsigned int length) {
  ESP_LOGI(TAG, "MQTT Data received");
  Serial.printf("TOPIC=%s\n", topic);
  Serial.printf("DATA=");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Handle Command
  if (strcmp(topic, mqtt_config.topic_cmd) == 0) {
    if (length == 4 && strncmp((char *)payload, "OPEN", 4) == 0) {
      ESP_LOGI(TAG, "Received OPEN command via MQTT");
      trigger_relay(); // Defined in main
      mqtt_manager_publish_status("OPENING");
    }
  }
}
#endif

void mqtt_manager_init(void) {
#ifdef ARDUINO
  mqtt_load_config();
  client.setServer(mqtt_config.broker_uri, 1883);
  client.setCallback(mqtt_callback);
  if (client.connect(get_mqtt_client_id().c_str())) {
    ESP_LOGI(TAG, "MQTT Connected");
    client.subscribe(mqtt_config.topic_cmd);
    client.publish(mqtt_config.topic_status, "ONLINE");
  } else {
    ESP_LOGE(TAG, "MQTT Connection failed");
  }
#else
  mqtt_load_config();

  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = mqtt_config.broker_uri,
  };

  client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler,
                                 client);
  esp_mqtt_client_start(client);
#endif
}

void mqtt_manager_publish_status(const char *status) {
#ifdef ARDUINO
  if (client.connected()) {
    client.publish(mqtt_config.topic_status, status);
  }
#else
  if (client) {
    esp_mqtt_client_publish(client, mqtt_config.topic_status, status, 0, 0, 0);
  }
#endif
}

void mqtt_manager_loop(void) {
#ifdef ARDUINO
  if (!client.connected()) {
    static unsigned long last_reconnect = 0;
    unsigned long now = millis();
    if (now - last_reconnect > 5000) {
      last_reconnect = now;
      if (WiFi.status() == WL_CONNECTED) {
        ESP_LOGI(TAG, "Attempting MQTT Reconnect...");
        // Re-load config or use existing? Existing is in mqtt_config
        // Re-set server just in case?
        client.setServer(mqtt_config.broker_uri, 1883);
        if (client.connect(get_mqtt_client_id().c_str())) {
          ESP_LOGI(TAG, "MQTT Connected");
          client.subscribe(mqtt_config.topic_cmd);
          client.publish(mqtt_config.topic_status, "ONLINE");
        }
      }
    }
  } else {
    client.loop();
  }
#endif
}
