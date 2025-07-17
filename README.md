# ESP32 摄像头实时视频流项目

基于ESP32-S3和OV3660摄像头传感器的WiFi实时视频流项目，支持通过Web浏览器访问摄像头画面。

## 📋 项目特性

- ✅ **WiFi连接**：连接到指定的WiFi热点
- ✅ **实时视频流**：MJPEG格式的实时视频传输
- ✅ **Web界面**：简洁的HTML界面，支持移动端
- ✅ **自适应传输**：根据网络状况动态调整帧率和质量
- ✅ **错误恢复**：网络异常时自动重试和恢复
- ✅ **Keep-Alive优化**：优化的HTTP连接管理

## 🛠 硬件要求

### ESP32开发板
- **芯片**：ESP32-S3 (支持PSRAM)
- **内存**：建议8MB PSRAM
- **WiFi**：内置2.4GHz WiFi

### 摄像头模块
- **传感器**：OV3660 (推荐) 或 OV2640
- **分辨率**：最高支持QVGA (320x240)
- **格式**：JPEG输出

### 引脚连接
```
摄像头引脚    ESP32-S3引脚
PWDN      ->  GPIO38
RESET     ->  未连接 (软件复位)
VSYNC     ->  GPIO6
HREF      ->  GPIO7
PCLK      ->  GPIO13
XCLK      ->  GPIO15
SDA       ->  GPIO4
SCL       ->  GPIO5
D0        ->  GPIO11
D1        ->  GPIO9
D2        ->  GPIO8
D3        ->  GPIO10
D4        ->  GPIO12
D5        ->  GPIO18
D6        ->  GPIO17
D7        ->  GPIO16
```

## 🚀 快速开始

### 1. 环境准备

确保已安装ESP-IDF v5.4或更高版本：

```bash
# 克隆ESP-IDF (如果未安装)
git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh
. ./export.sh
```

### 2. 项目配置

克隆并配置项目：

```bash
# 克隆项目
git clone <your-repo-url>
cd espcamera_new

# 配置WiFi信息
# 编辑 main/wifi_streaming.h 文件
```

在 `main/wifi_streaming.h` 中修改WiFi配置：

```c
#define WIFI_SSID "你的WiFi名称"
#define WIFI_PASSWORD "你的WiFi密码"
```

### 3. 编译和烧录

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 编译项目
idf.py build

# 烧录到开发板
idf.py -p COM端口 flash monitor
```

### 4. 访问视频流

1. 启动后，查看串口输出获取ESP32的IP地址
2. 在浏览器中访问：`http://ESP32的IP地址`
3. 即可看到实时视频流

## 📁 项目结构

```
espcamera_new/
├── main/
│   ├── main.c              # 主程序入口
│   ├── wifi_streaming.c    # WiFi和视频流处理
│   ├── wifi_streaming.h    # 头文件
│   └── CMakeLists.txt      # 组件配置
├── components/             # 外部组件
├── CMakeLists.txt         # 项目配置
└── README.md              # 项目说明
```

## ⚙️ 核心配置

### 摄像头配置
```c
// 在 main.c 中可调整以下参数：
.xclk_freq_hz = 20000000,    // 时钟频率 (20MHz)
.frame_size = FRAMESIZE_QVGA, // 分辨率 (320x240)
.jpeg_quality = 12,          // JPEG质量 (0-63，越小质量越高)
.fb_count = 1,               // 帧缓冲数量
```

### 网络优化
```c
// 在 wifi_streaming.c 中的关键参数：
config.keep_alive_idle = 7;     // Keep-alive空闲时间
config.keep_alive_interval = 3; // Keep-alive间隔
config.keep_alive_count = 5;    // Keep-alive重试次数
```

## 🔧 性能调优

### 提高帧率
- 降低 `skip_frames` 值 (当前为3)
- 减少 `frame_delay` 延时
- 增加 `chunk_size` 分块大小

### 降低延迟
- 减少 `vTaskDelay` 延时时间
- 优化 `max_frame_size` 阈值
- 考虑升级到WebSocket协议

### 提高稳定性
- 增加 `max_errors` 允许的错误次数
- 调整WiFi缓冲区大小
- 优化keep-alive参数

## 🐛 故障排除

### 常见问题

**1. 摄像头初始化失败**
```
错误：Camera Init Failed
解决：检查引脚连接，确认PSRAM已启用
```

**2. WiFi连接失败**
```
错误：WiFi连接失败
解决：检查SSID和密码，确认信号强度
```

**3. 视频流断开**
```
错误：httpd_sock_err: error in send
解决：调整keep-alive参数，增加错误重试次数
```

**4. 页面响应慢**
```
问题：访问IP地址响应很慢
解决：减少HTML内容，优化HTTP服务器配置
```

### 调试技巧

1. **查看详细日志**：
```bash
idf.py monitor
```

2. **检查内存使用**：
```c
ESP_LOGI(TAG, "Free heap: %d", esp_get_free_heap_size());
```

3. **网络状态监控**：
观察串口输出的网络错误和重连信息

## 📊 性能指标

### 当前性能
- **帧率**：约2-4 FPS
- **分辨率**：320x240 (QVGA)
- **延迟**：200-500ms
- **带宽**：约50-100KB/s

### 优化潜力
- 使用WebSocket可降低延迟20-40%
- 优化JPEG质量可平衡画质和速度
- 调整分辨率可提高帧率

## 🔄 未来计划

- [ ] WebSocket协议支持
- [ ] 多分辨率切换
- [ ] 运动检测功能
- [ ] 图像存储功能
- [ ] OTA更新支持

## 📄 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

## 🤝 贡献

欢迎提交Issue和Pull Request来改进项目！

## 📞 联系方式

如有问题或建议，请通过以下方式联系：
- 提交 [GitHub Issue](链接)
- 邮箱：your.email@example.com

---

**注意**：请确保在实际部署时修改默认的WiFi密码和相关安全设置。