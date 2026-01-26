#include "data_manager.h"
#include "logging_macros.h"

#ifdef ARDUINO
#include <LittleFS.h>
#else
#include "esp_log.h"
#include "esp_random.h"
#include "esp_spiffs.h"

#endif
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static const char *TAG = "DATA_MANAGER";
static system_data_t sys_data;
static const char *DATA_FILE = "/spiffs/data.bin";

void data_manager_init(void) {
  ESP_LOGI(TAG, "Initializing Data Manager...");

  // Set defaults
  memset(&sys_data, 0, sizeof(system_data_t));

  // Try logs loading from file
#ifdef ARDUINO
  File f = LittleFS.open(DATA_FILE, "r");
  if (!f) {
    ESP_LOGW(TAG, "No data file found, creating new one");
    data_manager_save();
  } else {
    f.read((uint8_t *)&sys_data, sizeof(system_data_t));
    f.close();
    ESP_LOGI(TAG, "Data loaded. Users: %d", sys_data.user_count);
  }
#else
  FILE *f = fopen(DATA_FILE, "rb");
  if (f == NULL) {
    ESP_LOGW(TAG, "No data file found, creating new one");
    data_manager_save();
  } else {
    fread(&sys_data, sizeof(system_data_t), 1, f);
    fclose(f);
    ESP_LOGI(TAG, "Data loaded. Users: %d", sys_data.user_count);
  }
#endif
}

void data_manager_save(void) {
#ifdef ARDUINO
  File f = LittleFS.open(DATA_FILE, "w");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open data file for writing");
    return;
  }
  f.write((uint8_t *)&sys_data, sizeof(system_data_t));
  f.close();
  ESP_LOGI(TAG, "Data saved");
#else
  FILE *f = fopen(DATA_FILE, "wb");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open data file for writing");
    return;
  }
  fwrite(&sys_data, sizeof(system_data_t), 1, f);
  fclose(f);
  ESP_LOGI(TAG, "Data saved");
#endif
}

char *data_manager_generate_pin(void) {
  static char pin_buf[PIN_LENGTH];
  bool unique = false;

  while (!unique) {
#ifdef ARDUINO
    uint32_t num = random(10000);
#else
    uint32_t num = esp_random() % 10000;
#endif
    snprintf(pin_buf, PIN_LENGTH, "%04u", num);

    unique = true;
    for (int i = 0; i < MAX_USERS; i++) {
      if (sys_data.users[i].active &&
          strcmp(sys_data.users[i].pin, pin_buf) == 0) {
        unique = false;
        break;
      }
    }
  }
  return pin_buf;
}

bool data_manager_add_user(const char *name, user_type_t type, int limit) {
  // Find empty slot
  int slot = -1;
  for (int i = 0; i < MAX_USERS; i++) {
    if (!sys_data.users[i].active) {
      slot = i;
      break;
    }
  }

  if (slot == -1) {
    ESP_LOGE(TAG, "User list full");
    return false;
  }

  user_t *u = &sys_data.users[slot];
  strncpy(u->name, name, NAME_LENGTH - 1);
  strcpy(u->pin, data_manager_generate_pin());
  u->type = type;
  u->active = true;

  // Set limits
  if (type == USER_TYPE_DATE_LIMIT) {
    // limit is days from now
    struct timeval tv;
    gettimeofday(&tv, NULL);
    u->expiry_date = tv.tv_sec + (limit * 24 * 3600);
  } else if (type == USER_TYPE_COUNT_LIMIT || type == USER_TYPE_ONE_TIME) {
    u->access_count_remaining = (type == USER_TYPE_ONE_TIME) ? 1 : limit;
  }

  sys_data.user_count++;
  data_manager_save();
  ESP_LOGI(TAG, "User added: %s (PIN: %s)", u->name, u->pin);
  return true;
}

bool data_manager_delete_user(const char *pin) {
  for (int i = 0; i < MAX_USERS; i++) {
    if (sys_data.users[i].active && strcmp(sys_data.users[i].pin, pin) == 0) {
      sys_data.users[i].active = false;
      sys_data.user_count--;
      data_manager_save();
      return true;
    }
  }
  return false;
}

// Brute Force Protection
static int failed_attempts = 0;
static int64_t lockout_timestamp = 0;
#define MAX_FAILED_ATTEMPTS 5
#define LOCKOUT_DURATION_SEC 300 // 5 minutes

bool data_manager_validate_pin(const char *pin, char *user_name_out) {
  struct timeval tv;
  gettimeofday(&tv, NULL);

  // Check if locked out
  if (lockout_timestamp > 0) {
    if (tv.tv_sec < lockout_timestamp) {
      ESP_LOGW(TAG, "System Locked Out. Wait %lld seconds.",
               lockout_timestamp - tv.tv_sec);
      return false;
    } else {
      // Lockout expired
      lockout_timestamp = 0;
      failed_attempts = 0;
      ESP_LOGI(TAG, "Lockout expired. System unlocked.");
    }
  }

  for (int i = 0; i < MAX_USERS; i++) {
    user_t *u = &sys_data.users[i];
    if (u->active && strcmp(u->pin, pin) == 0) {
      // Check limits
      if (u->type == USER_TYPE_DATE_LIMIT) {
        if (tv.tv_sec > u->expiry_date) {
          ESP_LOGW(TAG, "User %s expired", u->name);
          data_manager_log_access(u->name, false, "Expired (Time)");
          return false;
        }
      } else if (u->type == USER_TYPE_COUNT_LIMIT ||
                 u->type == USER_TYPE_ONE_TIME) {
        if (u->access_count_remaining <= 0) {
          ESP_LOGW(TAG, "User %s expired (count)", u->name);
          data_manager_log_access(u->name, false, "Expired (Count)");
          return false;
        }
        // Decrement count
        u->access_count_remaining--;

        // If One-Time and used, deactivate immediately (user request:
        // "Auto-delete")
        if (u->type == USER_TYPE_ONE_TIME && u->access_count_remaining == 0) {
          ESP_LOGI(TAG, "OTP User %s used. Deactivating.", u->name);
          u->active = false;
          sys_data.user_count--;
        }
        data_manager_save(); // Update state
      }

      // Check Schedule
      // Convert struct timeval to tm
      time_t now = tv.tv_sec;
      struct tm *timeinfo = localtime(&now);

      // Check Days (Bit 0 = Sun)
      // allowed_days: 0 means no restriction? Or 0 means NO access?
      // Let's assume default (from invalid init) might be 0.
      // If we want 0 to mean "All Days" we need to handle it.
      // But if we initialize to 0xFF or 0x7F it's better.
      // Let's assume if it is NOT 0, we check. If 0, maybe we block or assume
      // all? Strict security: 0 = NO access. But legacy users have 0. Since we
      // use memset 0, logic should probably treat 0 as "All Access" for
      // backward compatibility OR we must migrate data. Let's treat 0 as
      // "Access All Days" for now to avoid breaking existing users.
      if (u->allowed_days != 0) {
        if (!((u->allowed_days >> timeinfo->tm_wday) & 1)) {
          ESP_LOGW(TAG, "User %s denied (Day Restriction)", u->name);
          data_manager_log_access(u->name, false, "Denied (Schedule Day)");
          return false;
        }
      }

      // Check Time Window
      if (u->start_time != u->end_time) { // If equal, assume no restriction
        uint16_t current_mins = timeinfo->tm_hour * 60 + timeinfo->tm_min;
        bool in_window = false;
        if (u->start_time < u->end_time) {
          if (current_mins >= u->start_time && current_mins < u->end_time)
            in_window = true;
        } else {
          // Crossover 24h (e.g. 23:00 to 02:00)
          if (current_mins >= u->start_time || current_mins < u->end_time)
            in_window = true;
        }

        if (!in_window) {
          ESP_LOGW(TAG, "User %s denied (Time Restriction)", u->name);
          data_manager_log_access(u->name, false, "Denied (Schedule Time)");
          return false;
        }
      }

      if (user_name_out)
        strcpy(user_name_out, u->name);
      data_manager_log_access(u->name, true, "Access Granted");

      // Reset failed attempts on success
      failed_attempts = 0;
      return true;
    }
  }

  // Increment failed attempts
  failed_attempts++;
  ESP_LOGW(TAG, "Invalid PIN. Attempt %d/%d", failed_attempts,
           MAX_FAILED_ATTEMPTS);

  if (failed_attempts >= MAX_FAILED_ATTEMPTS) {
    lockout_timestamp = tv.tv_sec + LOCKOUT_DURATION_SEC;
    ESP_LOGE(TAG, "Multiple failures. System LOCKED for 5 minutes.");
    data_manager_log_access("System", false, "Security Lockout");
  } else {
    data_manager_log_access("Unknown", false, "Invalid PIN");
  }

  return false;
}

// File Logging Helper
void log_to_file(long timestamp, const char *user, bool granted,
                 const char *details) {
#ifdef ARDUINO
  // Check file size and rotate if needed
  if (LittleFS.exists("/access.log")) {
    File f_check = LittleFS.open("/access.log", "r");
    if (f_check && f_check.size() > 50 * 1024) { // 50KB limit
      f_check.close();
      ESP_LOGI(TAG, "Log file full, rotating...");
      LittleFS.remove("/access.log.bak");
      LittleFS.rename("/access.log", "/access.log.bak");
    } else {
      if (f_check)
        f_check.close();
    }
  }

  File f = LittleFS.open("/access.log", "a");
  if (f) {
    // CSV Format: Timestamp,User,Granted,Details
    f.printf("%ld,%s,%d,%s\n", timestamp, user, granted, details);
    f.close();
  } else {
    ESP_LOGE(TAG, "Failed to write to access.log");
  }

#else
  // Check file size and rotate if needed
  struct stat st;
  if (stat("/spiffs/access.log", &st) == 0) {
    if (st.st_size > 50 * 1024) { // 50KB limit
      ESP_LOGI(TAG, "Log file full, rotating...");
      unlink("/spiffs/access.log.bak");
      rename("/spiffs/access.log", "/spiffs/access.log.bak");
    }
  }

  FILE *f = fopen("/spiffs/access.log", "a");
  if (f) {
    // CSV Format: Timestamp,User,Granted,Details
    fprintf(f, "%ld,%s,%d,%s\n", timestamp, user, granted, details);
    fclose(f);
  } else {
    ESP_LOGE(TAG, "Failed to write to access.log");
  }
#endif
}

void data_manager_log_access(const char *name, bool granted,
                             const char *details) {
  int idx = sys_data.log_head;
  struct timeval tv;
  gettimeofday(&tv, NULL);

  // Update RAM Buffer (for UI)
  sys_data.logs[idx].timestamp = tv.tv_sec;
  strncpy(sys_data.logs[idx].user_name, name, NAME_LENGTH - 1);
  sys_data.logs[idx].granted = granted;
  strncpy(sys_data.logs[idx].details, details, 31);

  sys_data.log_head = (sys_data.log_head + 1) % MAX_LOGS;

  // Save RAM state (Users + Recent Logs)
  data_manager_save();

  // Persist to File (History)
  log_to_file(tv.tv_sec, name, granted, details);
}

system_data_t *data_manager_get_data(void) { return &sys_data; }
