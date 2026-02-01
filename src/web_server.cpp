#include "web_server.h"
#include "data_manager.h"
#include "logging_macros.h"
#include "mqtt_manager.h"

// Admin password hash (SHA256 of "Baracuda1106")
static const char *ADMIN_PASS_HASH =
    "377c977eb381cfd5ae17467fb99bb376069c1b85cc18fdcab81bf0d3fa062563";

#ifdef ARDUINO
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <bearssl/bearssl.h>

ESP8266WebServer server(80);
static const char *TAG = "WEB_SERVER";

// Helper to calculate SHA256 using BearSSL
void sha256_string(const char *str, char outputBuffer[65]) {
  br_sha256_context ctx;
  br_sha256_init(&ctx);
  br_sha256_update(&ctx, (const void *)str, strlen(str));
  uint8_t hash[32];
  br_sha256_out(&ctx, hash);

  for (int i = 0; i < 32; i++) {
    sprintf(outputBuffer + (i * 2), "%02x", hash[i]);
  }
  outputBuffer[64] = 0;
}

// Handler: Verify PIN
void handle_api_verify_pin() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  const char *pin = doc["pin"];
  char user_name[32];
  if (pin && data_manager_validate_pin(pin, user_name)) {
    trigger_relay();
    server.send(200, "application/json", "{\"status\":\"granted\"}");
  } else {
    server.send(401, "application/json", "{\"status\":\"denied\"}");
  }
}

// Handler: Login
void handle_api_login() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  const char *password = doc["password"];
  if (password) {
    char hash[65];
    sha256_string(password, hash);
    ESP_LOGI(TAG, "Login Attempt: Pass='%s', Hash='%s'", password, hash);
    ESP_LOGI(TAG, "Expected Hash='%s'", ADMIN_PASS_HASH);
    ESP_LOGI(TAG, "Strcmp Result: %d", strcmp(hash, ADMIN_PASS_HASH));

    if (strcmp(hash, ADMIN_PASS_HASH) == 0) {
      server.send(200, "application/json", "{\"status\":\"ok\"}");
      return;
    }
  } else {
    ESP_LOGE(TAG, "Login Attempt: Password field missing or null");
  }
  server.send(401, "application/json", "{\"status\":\"denied\"}");
}

// Handler: Get Users
void handle_api_get_users() {
  system_data_t *data = data_manager_get_data();
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();

  for (int i = 0; i < MAX_USERS; i++) {
    if (data->users[i].active) {
      JsonObject user = array.add<JsonObject>();
      user["name"] = data->users[i].name;
      user["pin"] = data->users[i].pin;
      user["type"] = data->users[i].type;
      user["expiry"] = data->users[i].expiry_date;
      user["remaining"] = data->users[i].access_count_remaining;
    }
  }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Handler: Add User
void handle_api_add_user() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const char *name = doc["name"];
  int typeInt = doc["type"];
  int limit = doc["limit"];

  if (data_manager_add_user(name, (user_type_t)typeInt, limit)) {
    // Schedule update logic
    system_data_t *data = data_manager_get_data();
    for (int i = 0; i < MAX_USERS; i++) {
      if (data->users[i].active && strcmp(data->users[i].name, name) == 0) {
        if (!doc["start"].isNull())
          data->users[i].start_time = doc["start"];
        if (!doc["end"].isNull())
          data->users[i].end_time = doc["end"];
        if (!doc["days"].isNull())
          data->users[i].allowed_days = doc["days"];
        data_manager_save();
        break;
      }
    }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(500, "application/json", "{\"error\":\"Failed to add user\"}");
  }
}

// Handler: Delete User
void handle_api_delete_user() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  const char *pin = doc["pin"];
  if (pin && data_manager_delete_user(pin)) {
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(500, "application/json", "{\"error\":\"Failed to delete\"}");
  }
}

// Handler: Get Logs
void handle_api_get_logs() {
  system_data_t *data = data_manager_get_data();
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();

  for (int i = 0; i < MAX_LOGS; i++) {
    if (data->logs[i].timestamp != 0) {
      JsonObject log = array.add<JsonObject>();
      log["time"] = data->logs[i].timestamp;
      log["user"] = data->logs[i].user_name;
      log["granted"] = data->logs[i].granted;
      log["details"] = data->logs[i].details;
    }
  }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Handler: Open Gate
void handle_api_open_gate() {
  trigger_relay();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// Handler: Get MQTT
void handle_api_get_mqtt() {
  char uri[64], cmd[64], status[64];
  mqtt_manager_get_config(uri, cmd, status);
  JsonDocument doc;
  doc["uri"] = uri;
  doc["cmd_topic"] = cmd;
  doc["status_topic"] = status;
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Handler: Set MQTT
void handle_api_set_mqtt() {
  if (!server.hasArg("plain"))
    return;
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  const char *uri = doc["uri"];
  const char *cmd = doc["cmd_topic"];
  const char *status = doc["status_topic"];

  if (uri) {
    mqtt_manager_update_config(uri, cmd, status);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(500, "application/json", "{\"error\":\"Invalid config\"}");
  }
}

// Handler: Static Files Logic
bool handleFileReadLogic(const String& path) {
  String contentType = "text/plain";
  if (path.endsWith(".html"))
    contentType = "text/html";
  else if (path.endsWith(".css"))
    contentType = "text/css";
  else if (path.endsWith(".js"))
    contentType = "application/javascript";
  else if (path.endsWith(".ico"))
    contentType = "image/x-icon";
  else if (path.endsWith(".png"))
    contentType = "image/png";

  if (LittleFS.exists(path)) {
    File file = LittleFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

// Handler: Static Files Wrapper (Optimization: avoid copy unless needed)
bool handleFileRead(const String& path) {
  if (path.endsWith("/"))
    return handleFileReadLogic(path + "index.html");
  if (path == "/admin")
    return handleFileReadLogic(path + ".html");
  return handleFileReadLogic(path);
}

void start_web_server(void) {
  ESP_LOGI(TAG, "Starting Web Server...");

  // API Routes
  server.on("/api/access/verify", HTTP_POST, handle_api_verify_pin);
  server.on("/api/auth/login", HTTP_POST, handle_api_login);

  server.on("/api/admin/users", HTTP_GET, handle_api_get_users);
  server.on("/api/admin/users", HTTP_POST, handle_api_add_user);
  server.on("/api/admin/users", HTTP_DELETE, handle_api_delete_user);

  server.on("/api/admin/logs", HTTP_GET, handle_api_get_logs);
  server.on("/api/admin/logs/download", HTTP_GET, []() {
    // Simply stream the file
    if (LittleFS.exists("/access.log")) {
      File f = LittleFS.open("/access.log", "r");
      server.streamFile(f, "text/csv");
      f.close();
    } else {
      server.send(404, "text/plain", "Log file not found");
    }
  });

  server.on("/api/admin/open", HTTP_POST, handle_api_open_gate);
  server.on("/api/admin/mqtt", HTTP_GET, handle_api_get_mqtt);
  server.on("/api/admin/mqtt", HTTP_POST, handle_api_set_mqtt);

  // Static Fallback
  server.onNotFound([]() {
    if (!handleFileRead(server.uri())) {
      server.send(404, "text/plain", "404: Not Found");
    }
  });

  server.begin();
  ESP_LOGI(TAG, "Web Server started on port 80");
}

void stop_web_server(void) { server.stop(); }

void web_server_loop(void) { server.handleClient(); }

#else // !ARDUINO -- KEEPING ORIGINAL IDF IMPLEMENTATION BELOW

#include <cJSON.h>
#include <esp_log.h>
#include <esp_spiffs.h>
#include <mbedtls/md.h>

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

// Helper to calculate SHA256 of input
void sha256_string(const char *str, char outputBuffer[65]) {
  unsigned char hash[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const unsigned char *)str, strlen(str));
  mbedtls_md_finish(&ctx, hash);
  mbedtls_md_free(&ctx);

  for (int i = 0; i < 32; i++) {
    sprintf(outputBuffer + (i * 2), "%02x", hash[i]);
  }
  outputBuffer[64] = 0;
}

// Handler for serving static files from SPIFFS
static esp_err_t static_file_handler(httpd_req_t *req) {
  char filepath[1024];

  if (strcmp(req->uri, "/") == 0) {
    strcpy(filepath, "/spiffs/data/index.html");
  } else if (strcmp(req->uri, "/admin") == 0) {
    strcpy(filepath, "/spiffs/data/admin.html");
  } else {
    snprintf(filepath, sizeof(filepath), "/spiffs/data%s", req->uri);
  }

  FILE *f = fopen(filepath, "r");
  if (f == NULL) {
    ESP_LOGE(TAG, "File not found: %s", filepath);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  if (strstr(filepath, ".css")) {
    httpd_resp_set_type(req, "text/css");
  } else if (strstr(filepath, ".js")) {
    httpd_resp_set_type(req, "application/javascript");
  } else if (strstr(filepath, ".html")) {
    httpd_resp_set_type(req, "text/html");
  }

  char chunk[1024];
  size_t chunksize;
  while ((chunksize = fread(chunk, 1, sizeof(chunk), f)) > 0) {
    if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
      fclose(f);
      return ESP_FAIL;
    }
  }
  fclose(f);
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

// API: Verify PIN
static esp_err_t api_verify_pin_handler(httpd_req_t *req) {
  char buf[100];
  int ret, remaining = req->content_len;
  if (remaining >= sizeof(buf)) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  if ((ret = httpd_req_recv(req, buf, remaining)) <= 0) {
    return ESP_FAIL;
  }
  buf[ret] = 0;

  cJSON *json = cJSON_Parse(buf);
  cJSON *pin_item = cJSON_GetObjectItem(json, "pin");

  if (!pin_item || !cJSON_IsString(pin_item)) {
    httpd_resp_send_500(req);
    cJSON_Delete(json);
    return ESP_FAIL;
  }

  char user_name[32];
  bool valid = data_manager_validate_pin(pin_item->valuestring, user_name);
  cJSON_Delete(json);

  if (valid) {
    trigger_relay();
    httpd_resp_sendstr(req, "{\"status\":\"granted\"}");
  } else {
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "{\"status\":\"denied\"}");
  }
  return ESP_OK;
}

// API: Admin Login
static esp_err_t api_login_handler(httpd_req_t *req) {
  char buf[100];
  int ret, remaining = req->content_len;
  if (remaining >= sizeof(buf))
    return ESP_FAIL;
  if ((ret = httpd_req_recv(req, buf, remaining)) <= 0)
    return ESP_FAIL;
  buf[ret] = 0;

  cJSON *json = cJSON_Parse(buf);
  cJSON *pass_item = cJSON_GetObjectItem(json, "password");

  if (!pass_item || !cJSON_IsString(pass_item)) {
    cJSON_Delete(json);
    return ESP_FAIL;
  }

  char hash[65];
  sha256_string(pass_item->valuestring, hash);
  cJSON_Delete(json);

  if (strcmp(hash, ADMIN_PASS_HASH) == 0) {
    // Set a simple cookie or token logic. For simplicity, we just return OK and
    // client stores a flag.
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  } else {
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid password");
  }
  return ESP_OK;
}

// API: Get Users
static esp_err_t api_get_users_handler(httpd_req_t *req) {
  system_data_t *data = data_manager_get_data();
  cJSON *root = cJSON_CreateArray();

  for (int i = 0; i < MAX_USERS; i++) {
    if (data->users[i].active) {
      cJSON *user = cJSON_CreateObject();
      cJSON_AddStringToObject(user, "name", data->users[i].name);
      cJSON_AddStringToObject(user, "pin", data->users[i].pin);
      cJSON_AddNumberToObject(user, "type", data->users[i].type);
      cJSON_AddNumberToObject(user, "expiry", data->users[i].expiry_date);
      cJSON_AddNumberToObject(user, "remaining",
                              data->users[i].access_count_remaining);
      cJSON_AddItemToArray(root, user);
    }
  }

  const char *json_str = cJSON_Print(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);
  cJSON_Delete(root);
  free((void *)json_str);
  return ESP_OK;
}

// API: Add User
static esp_err_t api_add_user_handler(httpd_req_t *req) {
  char buf[200];
  int ret, remaining = req->content_len;
  if (remaining >= sizeof(buf))
    return ESP_FAIL;
  if ((ret = httpd_req_recv(req, buf, remaining)) <= 0)
    return ESP_FAIL;
  buf[ret] = 0;

  cJSON *json = cJSON_Parse(buf);
  cJSON *name = cJSON_GetObjectItem(json, "name");
  cJSON *type = cJSON_GetObjectItem(json, "type");
  cJSON *limit = cJSON_GetObjectItem(json, "limit");
  cJSON *start = cJSON_GetObjectItem(json, "start");
  cJSON *end = cJSON_GetObjectItem(json, "end");
  cJSON *days = cJSON_GetObjectItem(json, "days");

  if (data_manager_add_user(name->valuestring, type->valueint,
                            limit ? limit->valueint : 0)) {
    // Set Schedule if provided
    system_data_t *data = data_manager_get_data();
    // Since add_user adds to next slot, we find the user by name to update
    // schedule Inoptimal but simple for prototype: iterate and match name + pin
    // just generated? Actually data_manager_add_user returns bool. Let's modify
    // data_manager_add_user or just set it manually here by finding the last
    // added? Or better: update data_manager_add_user to accept these? For
    // minimal changes: find the user we just added. Assumption: Last active
    // user? Or searching by name.
    for (int i = 0; i < MAX_USERS; i++) {
      if (data->users[i].active &&
          strcmp(data->users[i].name, name->valuestring) == 0) {
        if (start)
          data->users[i].start_time = start->valueint;
        if (end)
          data->users[i].end_time = end->valueint;
        if (days)
          data->users[i].allowed_days = days->valueint;
        data_manager_save();
        break;
      }
    }
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  } else {
    httpd_resp_send_500(req);
  }
  cJSON_Delete(json);
  return ESP_OK;
}

// API: Delete User
static esp_err_t api_delete_user_handler(httpd_req_t *req) {
  char buf[100];
  int ret, remaining = req->content_len;
  if (remaining >= sizeof(buf))
    return ESP_FAIL;
  if ((ret = httpd_req_recv(req, buf, remaining)) <= 0)
    return ESP_FAIL;
  buf[ret] = 0;

  cJSON *json = cJSON_Parse(buf);
  cJSON *pin = cJSON_GetObjectItem(json, "pin");

  if (data_manager_delete_user(pin->valuestring)) {
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  } else {
    httpd_resp_send_500(req);
  }
  cJSON_Delete(json);
  return ESP_OK;
}

// API: Download Log File
static esp_err_t api_download_logs_handler(httpd_req_t *req) {
  FILE *f = fopen("/spiffs/access.log", "r");
  if (f == NULL) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "text/csv");
  httpd_resp_set_hdr(req, "Content-Disposition",
                     "attachment; filename=\"access_history.csv\"");

  char chunk[1024];
  size_t chunksize;
  while ((chunksize = fread(chunk, 1, sizeof(chunk), f)) > 0) {
    if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
      fclose(f);
      return ESP_FAIL;
    }
  }
  fclose(f);
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

// API: Get Logs
static esp_err_t api_get_logs_handler(httpd_req_t *req) {
  system_data_t *data = data_manager_get_data();
  cJSON *root = cJSON_CreateArray();

  // Iterate circular buffer. Simplification: Just dump all non-zero logs for
  // now
  for (int i = 0; i < MAX_LOGS; i++) {
    if (data->logs[i].timestamp != 0) {
      cJSON *log = cJSON_CreateObject();
      cJSON_AddNumberToObject(log, "time", data->logs[i].timestamp);
      cJSON_AddStringToObject(log, "user", data->logs[i].user_name);
      cJSON_AddBoolToObject(log, "granted", data->logs[i].granted);
      cJSON_AddStringToObject(log, "details", data->logs[i].details);
      cJSON_AddItemToArray(root, log);
    }
  }

  const char *json_str = cJSON_Print(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);
  cJSON_Delete(root);
  free((void *)json_str);
  return ESP_OK;
}

// API: Open Gate (Direct Control)
static esp_err_t api_open_gate_handler(httpd_req_t *req) {
  trigger_relay();
  return ESP_OK;
}

// API: Get MQTT Config
static esp_err_t api_get_mqtt_handler(httpd_req_t *req) {
  char uri[64], cmd[64], status[64];
  mqtt_manager_get_config(uri, cmd, status);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "uri", uri);
  cJSON_AddStringToObject(root, "cmd_topic", cmd);
  cJSON_AddStringToObject(root, "status_topic", status);

  const char *json_str = cJSON_Print(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json_str);
  cJSON_Delete(root);
  free((void *)json_str);
  return ESP_OK;
}

// API: Set MQTT Config
static esp_err_t api_set_mqtt_handler(httpd_req_t *req) {
  char buf[256];
  int ret, remaining = req->content_len;
  if (remaining >= sizeof(buf))
    return ESP_FAIL;
  if ((ret = httpd_req_recv(req, buf, remaining)) <= 0)
    return ESP_FAIL;
  buf[ret] = 0;

  cJSON *json = cJSON_Parse(buf);
  cJSON *uri = cJSON_GetObjectItem(json, "uri");
  cJSON *cmd = cJSON_GetObjectItem(json, "cmd_topic");
  cJSON *status = cJSON_GetObjectItem(json, "status_topic");

  if (uri) {
    // Simple update logic
    mqtt_manager_update_config(uri->valuestring, cmd ? cmd->valuestring : NULL,
                               status ? status->valuestring : NULL);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  } else {
    httpd_resp_send_500(req);
  }
  cJSON_Delete(json);
  return ESP_OK;
}

void start_web_server(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t uri_root = {.uri = "/*",
                            .method = HTTP_GET,
                            .handler = static_file_handler,
                            .user_ctx = NULL};
    httpd_register_uri_handler(server, &uri_root);

    httpd_uri_t uri_verify = {.uri = "/api/access/verify",
                              .method = HTTP_POST,
                              .handler = api_verify_pin_handler};
    httpd_register_uri_handler(server, &uri_verify);

    httpd_uri_t uri_login = {.uri = "/api/auth/login",
                             .method = HTTP_POST,
                             .handler = api_login_handler};
    httpd_register_uri_handler(server, &uri_login);

    httpd_uri_t uri_users_get = {.uri = "/api/admin/users",
                                 .method = HTTP_GET,
                                 .handler = api_get_users_handler};
    httpd_register_uri_handler(server, &uri_users_get);

    httpd_uri_t uri_users_add = {.uri = "/api/admin/users",
                                 .method = HTTP_POST,
                                 .handler = api_add_user_handler};
    httpd_register_uri_handler(server, &uri_users_add);

    httpd_uri_t uri_users_del = {.uri = "/api/admin/users",
                                 .method = HTTP_DELETE,
                                 .handler = api_delete_user_handler};
    httpd_register_uri_handler(server, &uri_users_del);

    httpd_uri_t uri_logs = {.uri = "/api/admin/logs",
                            .method = HTTP_GET,
                            .handler = api_get_logs_handler};
    httpd_register_uri_handler(server, &uri_logs);

    httpd_uri_t uri_open = {.uri = "/api/admin/open",
                            .method = HTTP_POST,
                            .handler = api_open_gate_handler};
    httpd_register_uri_handler(server, &uri_open);

    httpd_uri_t uri_dl_logs = {.uri = "/api/admin/logs/download",
                               .method = HTTP_GET,
                               .handler = api_download_logs_handler};
    httpd_register_uri_handler(server, &uri_dl_logs);

    httpd_uri_t uri_mqtt_get = {.uri = "/api/admin/mqtt",
                                .method = HTTP_GET,
                                .handler = api_get_mqtt_handler};
    httpd_register_uri_handler(server, &uri_mqtt_get);

    httpd_uri_t uri_mqtt_set = {.uri = "/api/admin/mqtt",
                                .method = HTTP_POST,
                                .handler = api_set_mqtt_handler};
    httpd_register_uri_handler(server, &uri_mqtt_set);
  }
}

void stop_web_server(void) {
  if (server) {
    httpd_stop(server);
  }
}
#endif
