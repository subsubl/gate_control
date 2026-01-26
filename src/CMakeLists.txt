idf_component_register(SRCS "main.c" "web_server.c" "data_manager.c" "mqtt_manager.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_server esp_wifi nvs_flash esp_event driver spiffs cjson mbedtls esp_driver_gpio mqtt freertos esp_netif)

