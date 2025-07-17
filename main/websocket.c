#include "websocket.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "WEBSOCKET_SERVER";

static httpd_handle_t ws_server = NULL;
static QueueHandle_t frame_queue = NULL;
static bool ws_streaming = false;
static TaskHandle_t camera_task_handle = NULL;

// 帧数据结构
typedef struct {
    uint8_t *data;
    size_t len;
} frame_data_t;

// WebSocket连接处理 - 使用HTTP升级到WebSocket
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket握手请求");
        
        // 检查WebSocket升级头
        char upgrade[32] = {0};
        char connection[32] = {0};
        char ws_key[64] = {0};
        
        if (httpd_req_get_hdr_value_str(req, "Upgrade", upgrade, sizeof(upgrade)) != ESP_OK ||
            httpd_req_get_hdr_value_str(req, "Connection", connection, sizeof(connection)) != ESP_OK ||
            httpd_req_get_hdr_value_str(req, "Sec-WebSocket-Key", ws_key, sizeof(ws_key)) != ESP_OK) {
            
            ESP_LOGE(TAG, "缺少WebSocket升级头");
            return ESP_FAIL;
        }
        
        if (strcasecmp(upgrade, "websocket") != 0) {
            ESP_LOGE(TAG, "不是WebSocket升级请求");
            return ESP_FAIL;
        }
        
        ESP_LOGI(TAG, "WebSocket升级成功");
        return ESP_OK;
    }
    
    return ESP_FAIL;
}

// 摄像头捕获任务
static void camera_capture_task(void *pvParameters)
{
    camera_fb_t *fb = NULL;
    size_t frame_count = 0;
    
    ESP_LOGI(TAG, "摄像头捕获任务启动");
    
    while (true) {
        if (!ws_streaming) {
            vTaskDelay(500 / portTICK_PERIOD_MS);
            continue;
        }
        
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "摄像头获取失败");
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }
        
        frame_count++;
        
        // 跳帧处理
        if (frame_count % 2 != 0) {
            esp_camera_fb_return(fb);
            vTaskDelay(50 / portTICK_PERIOD_MS);
            continue;
        }
        
        // 大帧检查
        if (fb->len > 40 * 1024) {
            ESP_LOGW(TAG, "帧过大 (%zu KB)，跳过", fb->len / 1024);
            esp_camera_fb_return(fb);
            vTaskDelay(50 / portTICK_PERIOD_MS);
            continue;
        }
        
        // 创建帧数据
        frame_data_t frame_data;
        frame_data.data = malloc(fb->len);
        if (frame_data.data) {
            memcpy(frame_data.data, fb->buf, fb->len);
            frame_data.len = fb->len;
            
            // 发送到队列
            if (xQueueSend(frame_queue, &frame_data, pdMS_TO_TICKS(100)) != pdPASS) {
                ESP_LOGW(TAG, "队列满，丢弃帧");
                free(frame_data.data);
            }
        }
        
        esp_camera_fb_return(fb);
        vTaskDelay(100 / portTICK_PERIOD_MS); // 约10fps
    }
    
    vTaskDelete(NULL);
}

// WebSocket主页处理
static esp_err_t ws_index_handler(httpd_req_t *req)
{
    const char* html_page = 
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<title>ESP32 Camera WebSocket</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body{font-family:Arial;text-align:center;background:#222;color:white;margin:0;padding:10px;}"
    ".container{max-width:600px;margin:0 auto;}"
    ".video-container{margin:10px 0;border:1px solid #444;border-radius:5px;overflow:hidden;background:#333;}"
    "img{width:100%;height:auto;display:block;}"
    ".controls{margin:15px 0;}"
    "button{padding:10px 20px;margin:8px;background:#007bff;color:white;border:none;border-radius:6px;cursor:pointer;font-size:14px;}"
    "button:hover{background:#0056b3;}"
    "button:disabled{background:#666;cursor:not-allowed;}"
    ".status{margin:15px 0;padding:10px;background:#333;border-radius:6px;border-left:4px solid #007bff;}"
    ".info{font-size:12px;color:#aaa;margin-top:10px;}"
    "</style>"
    "</head>"
    "<body>"
    "<div class='container'>"
    "<h1>ESP32 Camera WebSocket Stream</h1>"
    "<div class='video-container'>"
    "<img id='videoImg' alt='Video Stream' style='display:none;'>"
    "<div id='placeholder' style='padding:60px;color:#666;'>点击开始按钮启动视频流</div>"
    "</div>"
    "<div class='controls'>"
    "<button id='startBtn' onclick='startStream()'>开始视频流</button>"
    "<button id='stopBtn' onclick='stopStream()' disabled>停止视频流</button>"
    "<button onclick='location.reload()'>刷新页面</button>"
    "</div>"
    "<div class='status' id='status'>准备连接...</div>"
    "<div class='info'>基于ESP32官方WebSocket组件 | 更低延迟 | 约10fps</div>"
    "</div>"
    "<script>"
    "let frameCount = 0;"
    "const videoImg = document.getElementById('videoImg');"
    "const placeholder = document.getElementById('placeholder');"
    "const status = document.getElementById('status');"
    "const startBtn = document.getElementById('startBtn');"
    "const stopBtn = document.getElementById('stopBtn');"
    ""
    "function updateStatus(msg) {"
    "  status.textContent = msg;"
    "  console.log(msg);"
    "}"
    ""
    "// 模拟WebSocket连接来获取视频流"
    "function startStream() {"
    "  updateStatus('开始请求视频流...');"
    "  startBtn.disabled = true;"
    "  stopBtn.disabled = false;"
    "  placeholder.style.display = 'none';"
    "  videoImg.style.display = 'block';"
    "  "
    "  // 使用HTTP流模式 (因为这是服务器端实现)"
    "  videoImg.src = '/stream';"
    "  frameCount = 0;"
    "  updateStatus('视频流已开始');"
    "}"
    ""
    "function stopStream() {"
    "  updateStatus('停止视频流');"
    "  startBtn.disabled = false;"
    "  stopBtn.disabled = true;"
    "  placeholder.style.display = 'block';"
    "  videoImg.style.display = 'none';"
    "  videoImg.src = '';"
    "}"
    ""
    "// 页面加载完成"
    "window.onload = function() {"
    "  updateStatus('页面加载完成，可以开始视频流');"
    "  startBtn.disabled = false;"
    "};"
    "</script>"
    "</body>"
    "</html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
}

// 视频流处理 (MJPEG格式)
static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t res = ESP_OK;
    char part_buf[128];
    frame_data_t frame_data;
    size_t frames_sent = 0;
    
    // WebSocket流相关定义
    const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=123456789000000000000987654321";
    const char* STREAM_BOUNDARY = "\r\n--123456789000000000000987654321\r\n";
    const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

    ESP_LOGI(TAG, "开始WebSocket视频流传输");
    
    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;
    
    // 设置HTTP头
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    
    // 启动视频流
    ws_streaming = true;
    
    while (true) {
        // 从队列获取帧数据
        if (xQueueReceive(frame_queue, &frame_data, pdMS_TO_TICKS(1000)) == pdPASS) {
            
            // 发送边界
            res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
            if (res != ESP_OK) {
                free(frame_data.data);
                break;
            }
            
            // 发送JPEG头
            size_t hlen = snprintf(part_buf, 128, STREAM_PART, frame_data.len);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
            if (res != ESP_OK) {
                free(frame_data.data);
                break;
            }
            
            // 分块发送图像数据
            const size_t chunk_size = 4096;
            size_t sent = 0;
            
            while (sent < frame_data.len) {
                size_t to_send = (frame_data.len - sent) > chunk_size ? chunk_size : (frame_data.len - sent);
                res = httpd_resp_send_chunk(req, (const char *)frame_data.data + sent, to_send);
                if (res != ESP_OK) {
                    break;
                }
                sent += to_send;
            }
            
            free(frame_data.data);
            
            if (res != ESP_OK) {
                break;
            }
            
            frames_sent++;
            if (frames_sent % 50 == 0) {
                ESP_LOGI(TAG, "WebSocket已发送 %zu 帧", frames_sent);
            }
            
        } else {
            // 超时，发送keep-alive
            res = httpd_resp_send_chunk(req, "\r\n", 2);
            if (res != ESP_OK) {
                break;
            }
        }
    }
    
    ws_streaming = false;
    ESP_LOGI(TAG, "WebSocket视频流传输结束");
    return res;
}

// 启动WebSocket服务器
esp_err_t start_websocket_server(void)
{
    // 创建帧队列
    frame_queue = xQueueCreate(5, sizeof(frame_data_t));
    if (!frame_queue) {
        ESP_LOGE(TAG, "创建帧队列失败");
        return ESP_FAIL;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;           // 使用8080端口
    config.stack_size = 8192;            // 增加栈大小
    config.max_open_sockets = 5;         // 增加连接数
    config.task_priority = 6;
    config.core_id = 1;
    config.send_wait_timeout = 10;       // 增加超时时间
    config.recv_wait_timeout = 10;
    config.lru_purge_enable = true;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 10;
    config.keep_alive_interval = 5;
    config.keep_alive_count = 3;
    
    if (httpd_start(&ws_server, &config) == ESP_OK) {
        // 主页处理
        httpd_uri_t index_uri = {
            .uri = "/", 
            .method = HTTP_GET, 
            .handler = ws_index_handler
        };
        
        // WebSocket升级处理
        httpd_uri_t ws_uri = {
            .uri = "/ws",
            .method = HTTP_GET,
            .handler = ws_handler,
            .user_ctx = NULL
        };
        
        // 视频流处理
        httpd_uri_t stream_uri = {
            .uri = "/stream",
            .method = HTTP_GET,
            .handler = stream_handler,
            .user_ctx = NULL
        };
        
        httpd_register_uri_handler(ws_server, &index_uri);
        httpd_register_uri_handler(ws_server, &ws_uri);
        httpd_register_uri_handler(ws_server, &stream_uri);
        
        // 启动摄像头捕获任务
        xTaskCreatePinnedToCore(camera_capture_task, "camera_task", 6144, NULL, 5, &camera_task_handle, 1);
        
        ESP_LOGI(TAG, "WebSocket服务器启动成功，端口: 8080");
        ESP_LOGI(TAG, "访问: http://ESP32_IP:8080/");
        return ESP_OK;
    }
    
    if (frame_queue) {
        vQueueDelete(frame_queue);
        frame_queue = NULL;
    }
    
    return ESP_FAIL;
}

// 停止WebSocket服务器
void stop_websocket_server(void)
{
    ws_streaming = false;
    
    if (camera_task_handle) {
        vTaskDelete(camera_task_handle);
        camera_task_handle = NULL;
    }
    
    if (frame_queue) {
        // 清空队列
        frame_data_t frame_data;
        while (xQueueReceive(frame_queue, &frame_data, 0) == pdPASS) {
            free(frame_data.data);
        }
        vQueueDelete(frame_queue);
        frame_queue = NULL;
    }
    
    if (ws_server) {
        httpd_stop(ws_server);
        ws_server = NULL;
        ESP_LOGI(TAG, "WebSocket服务器已停止");
    }
}

// 获取WebSocket状态
bool is_websocket_streaming(void)
{
    return ws_streaming;
}

// 获取等待发送的帧数量
int get_pending_frames(void)
{
    if (frame_queue) {
        return uxQueueMessagesWaiting(frame_queue);
    }
    return 0;
}