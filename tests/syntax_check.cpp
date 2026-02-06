// Define mocks are handled by include path and headers themselves
// But we need to implement symbols that are extern

#include "data_manager.h"
#include "mqtt_manager.h"
#include "web_server.h"

// Implement dummy symbols for linker
void trigger_relay(void) {}
system_data_t* data_manager_get_data(void) { static system_data_t d; return &d; }
bool data_manager_validate_pin(const char *pin, char *user_name_out) { return true; }
bool data_manager_add_user(const char *name, user_type_t type, int limit) { return true; }
void data_manager_save(void) {}
bool data_manager_delete_user(const char *pin) { return true; }
void mqtt_manager_get_config(char *uri, char *cmd, char *status) {}
void mqtt_manager_update_config(const char *uri, const char *cmd, const char *status) {}

// Include the source file directly
#include "../../src/web_server.cpp"

int main() {
    return 0;
}
