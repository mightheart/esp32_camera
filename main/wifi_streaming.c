#include "wifi_streaming.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "WIFI";

// WiFi事件组
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static httpd_handle_t stream_server = NULL;

// 视频流相关定义
#define STREAM_CONTENT_TYPE "multipart/x-mixed-replace;boundary=123456789000000000000987654321"
#define STREAM_BOUNDARY "\r\n--123456789000000000000987654321\r\n"
#define STREAM_PART "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n"

// WiFi事件处理
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "重试连接 %d/%d", s_retry_num, WIFI_MAXIMUM_RETRY);
        } else {
            // 达到最大重试次数，等待2秒后重置重试计数并继续
            ESP_LOGW(TAG, "达到最大重试次数 %d，继续重连...", WIFI_MAXIMUM_RETRY);
            
            s_retry_num = 0;  // 重置重试计数
            ESP_LOGI(TAG, "重置重试计数，继续尝试连接...");
            esp_wifi_connect();  // 继续尝试连接
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// WiFi初始化
esp_err_t wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // 针对手机热点优化的WiFi配置
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.dynamic_tx_buf_num = 16;     // 减少缓冲区（手机热点带宽有限）
    cfg.dynamic_rx_buf_num = 16;     
    cfg.static_tx_buf_num = 2;       // 减少静态缓冲区
    cfg.static_rx_buf_num = 6;       
    cfg.tx_buf_type = 1;             // 使用动态缓冲区
    
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件处理器
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
            // 手机热点优化设置
            .bssid_set = false,          // 不指定BSSID
            .channel = 0,                // 自动搜索信道
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // 重要：针对手机热点的优化设置
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));        // 禁用省电模式
    
    // 尝试设置带宽（手机热点可能不支持40MHz，会自动回退到20MHz）
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);     // 明确使用20MHz
    
    // 设置协议模式（优先使用802.11n）
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "开始WiFi连接，将持续重试直到成功...");

    // 等待连接
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, 
                                           WIFI_CONNECTED_BIT,    // 只等待连接成功
                                           pdFALSE, pdFALSE, 
                                           portMAX_DELAY);       // 无限等待
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi连接手机热点成功");
        
        // 连接成功后检查实际参数
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "连接信息 - RSSI: %d, 信道: %d, 认证模式: %d", 
                     ap_info.rssi, ap_info.primary, ap_info.authmode);
        }
        
        return ESP_OK;
    } 
    
    return ESP_FAIL;
    
}

// 视频流处理
static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[128];
    size_t frame_count = 0;
    size_t error_count = 0;
    size_t dropped_frames = 0;  // 统计丢帧数
    const size_t max_errors = 5;

    ESP_LOGI(TAG, "开始视频流传输");

    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;
    
    // 优化HTTP头
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "Keep-Alive", "timeout=5, max=100");  // 添加keep-alive参数

    while (true) {
        // 关键优化：立即获取最新帧，清空缓冲区
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "摄像头获取失败");
            vTaskDelay(50 / portTICK_PERIOD_MS);  // 减少延时
            continue;
        }
        
        frame_count++;

        // 更激进的跳帧策略
        int skip_frames = 2;  // 固定跳2帧，只发送1帧
        if (error_count > 1) {
            skip_frames = 12;  // 错误时跳更多
        }
        
        if (frame_count % skip_frames != 0) {
            esp_camera_fb_return(fb);
            dropped_frames++;
            
            // 重要：不要延时！立即获取下一帧
            continue;  
        }

        // 大帧检查 - 根据错误情况动态调整阈值
        size_t max_frame_size = (error_count > 2) ? 15 * 1024 : 25 * 1024;
        if (fb->len > max_frame_size) {
            ESP_LOGW(TAG, "帧过大 (%zu KB)，跳过", fb->len / 1024);
            esp_camera_fb_return(fb);
            dropped_frames++;
            continue;  // 不延时
        }

        // 发送边界
        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res != ESP_OK) {
            ESP_LOGW(TAG, "发送边界失败: %s (错误计数: %zu)", esp_err_to_name(res), error_count);
            esp_camera_fb_return(fb);

            // 检查是否是连接断开
            if (res == ESP_ERR_HTTPD_RESP_SEND || res == ESP_ERR_HTTPD_INVALID_REQ) {
                ESP_LOGI(TAG, "客户端已断开连接，结束视频流");
                break; // 直接退出循环
            }

            error_count++;
            if (error_count >= max_errors) {
                ESP_LOGI(TAG, "错误过多，暂停5秒后重试");
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                error_count = 0;
                continue;
            }
            vTaskDelay(500 / portTICK_PERIOD_MS);
            continue;
        }

        // 发送JPEG头
        size_t hlen = snprintf(part_buf, 128, STREAM_PART, fb->len);
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res != ESP_OK) {
            ESP_LOGW(TAG, "发送头部失败: %s (错误计数: %zu)", esp_err_to_name(res), error_count);
            esp_camera_fb_return(fb);

            if (res == ESP_ERR_HTTPD_RESP_SEND || res == ESP_ERR_HTTPD_INVALID_REQ) {
                ESP_LOGI(TAG, "客户端已断开连接，结束视频流");
                break;
            }

            error_count++;
            if (error_count >= max_errors) {
                ESP_LOGI(TAG, "错误过多，暂停5秒后重试");
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                error_count = 0;
                continue;
            }
            vTaskDelay(500 / portTICK_PERIOD_MS);
            continue;
        }

        // 分块发送图像数据
        const size_t chunk_size = 2024;
        size_t sent = 0;
        bool send_failed = false;

        while (sent < fb->len && !send_failed) {
            size_t to_send = (fb->len - sent) > chunk_size ? chunk_size : (fb->len - sent);

            res = httpd_resp_send_chunk(req, (const char *)fb->buf + sent, to_send);
            if (res != ESP_OK) {
                ESP_LOGW(TAG, "发送数据块失败: %s", esp_err_to_name(res));
                // 检查是否是连接断开
                if (res == ESP_ERR_HTTPD_RESP_SEND || res == ESP_ERR_HTTPD_INVALID_REQ) {
                    ESP_LOGI(TAG, "客户端已断开连接，结束视频流");
                    send_failed = true;
                    break;
                }
                send_failed = true;
                break;
            }
            sent += to_send;
            vTaskDelay(5 / portTICK_PERIOD_MS);
        }

        esp_camera_fb_return(fb);

        if (send_failed) {
            error_count++;
            if (error_count >= max_errors) {
                ESP_LOGI(TAG, "错误过多，暂停5秒后重试");
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                error_count = 0;
                continue;
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        // 发送成功，重置错误计数
        error_count = 0;

        // 自适应帧间延时
        int frame_delay = (error_count > 0) ? 200 : 50;
        vTaskDelay(frame_delay / portTICK_PERIOD_MS);
        
        // 状态输出
        if ((frame_count / skip_frames) % 20 == 0) {
            ESP_LOGI(TAG, "发送: %zu 帧, 丢弃: %zu 帧, 错误: %zu", 
                     frame_count / skip_frames, dropped_frames, error_count);
        }
    }
    
    ESP_LOGI(TAG, "视频流传输结束，总丢帧: %zu", dropped_frames);
    return res;
}

// 主页处理 - 优化响应速度
static esp_err_t index_handler(httpd_req_t *req)
{
    // 简化的HTML页面，减少内容大小
    const char* html_page = 
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<title>ESP32 Camera</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body{font-family:Arial;text-align:center;background:#222;color:white;margin:0;padding:10px;}"
    ".container{max-width:600px;margin:0 auto;}"
    ".video-container{margin:10px 0;border:1px solid #444;border-radius:5px;overflow:hidden;}"
    "img{width:100%;height:auto;display:block;}"
    "</style>"
    "</head>"
    "<body>"
    "<div class='container'>"
    "<h1>ESP32 Camera</h1>"
    "<div class='video-container'>"
    "<img src='/stream' alt='Stream'>"
    "</div>"
    "</div>"
    "</body>"
    "</html>";

    // 快速设置响应头并发送
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=300");  // 缓存5分钟
    return httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
}

// 启动HTTP服务器
esp_err_t start_streaming_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;            // 增加栈大小
    config.max_open_sockets = 2;         // 增加最大连接数
    config.task_priority = 6;            // 提高任务优先级
    config.core_id = 1;                  // 绑定核心1
    config.send_wait_timeout = 5;        // 增加发送超时时间
    config.recv_wait_timeout = 5;        // 增加接收超时时间
    config.lru_purge_enable = true;      // 启用LRU清理
    config.keep_alive_enable = true;     // 保持keep-alive
    config.keep_alive_idle = 7;          // keep-alive空闲时间
    config.keep_alive_interval = 3;      // keep-alive间隔
    config.keep_alive_count = 5;         // keep-alive重试次数

    
    if (httpd_start(&stream_server, &config) == ESP_OK) {
        httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
        httpd_uri_t stream_uri = {.uri = "/stream", .method = HTTP_GET, .handler = stream_handler};
        
        httpd_register_uri_handler(stream_server, &index_uri);
        httpd_register_uri_handler(stream_server, &stream_uri);
        
        ESP_LOGI(TAG, "HTTP服务器启动成功");
        return ESP_OK;
    }
    return ESP_FAIL;
}

// 停止服务器
void stop_streaming_server(void)
{
    if (stream_server) {
        httpd_stop(stream_server);
        stream_server = NULL;
    }
}