#ifndef WIFI_STREAMING_H
#define WIFI_STREAMING_H

#include "esp_err.h"

// WiFi配置 - 请修改为你的手机热点信息
#define WIFI_SSID "你的wifi名称"
#define WIFI_PASSWORD "你的wifi密码"
#define WIFI_MAXIMUM_RETRY 5

esp_err_t wifi_init_sta(void);
esp_err_t start_streaming_server(void);
void stop_streaming_server(void);

#endif