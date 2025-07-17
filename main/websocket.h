#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include "esp_err.h"
#include <stdbool.h>

// WebSocket服务器函数
esp_err_t start_websocket_server(void);
void stop_websocket_server(void);
bool is_websocket_streaming(void);
int get_pending_frames(void);

#endif