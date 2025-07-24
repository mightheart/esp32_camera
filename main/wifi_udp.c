#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <string.h>

#define UDP_BROADCAST_PORT 45678
#define UDP_BROADCAST_INTERVAL_MS 3000

static const char *TAG = "UDP_BROADCAST";

static void udp_broadcast_task(void *pvParameter)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct sockaddr_in broadcast_addr = {0};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(UDP_BROADCAST_PORT);
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    while (1) {
        // 获取本机IP
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            char msg[32];
            snprintf(msg, sizeof(msg), "ESP32CAM:%d.%d.%d.%d",
                     IP2STR(&ip_info.ip));
            // 发送带前缀的IP地址
            sendto(sock, msg, strlen(msg), 0,
                   (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
            ESP_LOGI(TAG, "UDP广播本机IP: %s", msg);
        }
        vTaskDelay(UDP_BROADCAST_INTERVAL_MS / portTICK_PERIOD_MS);
    }
    // 不会走到这里
    close(sock);
    vTaskDelete(NULL);
}

void start_udp_broadcast(void)
{
    xTaskCreatePinnedToCore(udp_broadcast_task, "udp_broadcast_task", 4096, NULL, 5, NULL, 1);
}