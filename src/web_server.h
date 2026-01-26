#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#ifdef ARDUINO
void start_web_server(void);
void stop_web_server(void);
void web_server_loop(void);
#else
#include <esp_http_server.h>
esp_err_t start_web_server(void);
void stop_web_server(void);
#endif

// Function to control relay (implemented in main)
void trigger_relay(void);

#endif // WEB_SERVER_H
