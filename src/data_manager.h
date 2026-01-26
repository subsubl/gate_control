#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_USERS 50
#define MAX_LOGS 50
#define PIN_LENGTH 5
#define NAME_LENGTH 32

typedef enum {
    USER_TYPE_UNLIMITED = 0,
    USER_TYPE_DATE_LIMIT = 1,
    USER_TYPE_COUNT_LIMIT = 2,
    USER_TYPE_ONE_TIME = 3
} user_type_t;

typedef struct {
    char name[NAME_LENGTH];
    char pin[PIN_LENGTH];
    user_type_t type;
    int64_t expiry_date; // Unix timestamp
    int access_count_remaining;
    bool active;
    uint16_t start_time; // Minutes from midnight
    uint16_t end_time;   // Minutes from midnight
    uint8_t allowed_days; // Bitmask: 0=Sun, 6=Sat
} user_t;

typedef struct {
    int64_t timestamp;
    char user_name[NAME_LENGTH];
    bool granted;
    char details[32]; // e.g., "Invalid PIN" or "Access Granted"
} access_log_t;

typedef struct {
    user_t users[MAX_USERS];
    access_log_t logs[MAX_LOGS];
    int log_head; // Circular buffer index
    int user_count;
} system_data_t;

void data_manager_init(void);
void data_manager_save(void);
bool data_manager_validate_pin(const char *pin, char *user_name_out);
bool data_manager_add_user(const char *name, user_type_t type, int limit); // limit is either count or days
bool data_manager_delete_user(const char *pin);
void data_manager_log_access(const char *name, bool granted, const char *details);
system_data_t *data_manager_get_data(void);
char* data_manager_generate_pin(void);

#endif // DATA_MANAGER_H
