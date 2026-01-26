#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <vector>


struct User {
  String name;
  String pin;
  int type; // 0=Unlim, 1=Temp, 2=Count, 3=OTP
  uint32_t expiry;
  int remaining;
  int start_min;
  int end_min;
  uint8_t allowed_days;
};

struct LogEntry {
  uint32_t timestamp;
  String user;
  bool granted;
  String details;
};

struct MqttConfig {
  String params; // Just holding it here if needed, or separate
  String broker = "test.mosquitto.org";
  String cmd_topic = "antigravity_gate/cmd";
  String status_topic = "antigravity_gate/status";
};

class DataManager {
public:
  void begin();

  // User Management
  void loadUsers();
  void saveUsers();
  bool verifyPin(String pin, String &userName);
  void addUser(User u);
  void deleteUser(String pin);
  std::vector<User> &getUsers() { return users; }

  // Logs
  void logAccess(String user, bool granted, String details);
  String getLogsJson();
  File getLogFile(); // For download

  // MQTT Config (stored in config.json)
  void loadConfig();
  void saveConfig();
  MqttConfig mqtt;

private:
  std::vector<User> users;
  // Helper to check schedule
  bool checkSchedule(User &u);
};

extern DataManager DB;

#endif
