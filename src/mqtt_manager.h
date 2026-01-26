#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <stdbool.h>

void mqtt_manager_init(void);
void mqtt_manager_publish_status(const char *status);
void mqtt_manager_get_config(char *uri, char *cmd, char *status);
void mqtt_manager_update_config(const char *uri, const char *cmd, const char *status);

#endif // MQTT_MANAGER_H
