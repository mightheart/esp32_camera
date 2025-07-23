idf_component_register(SRCS "websocket.c" "wifi_streaming.c" "main.c"

                    INCLUDE_DIRS 
                     "../components/esp32-camera/driver/include" 
                     "${IDF_PATH}/components/esp_driver_ledc/include"
                     "${IDF_PATH}/components/esp_driver_spi/include" 
                     "${IDF_PATH}/components/esp_driver_gpio/include" 
                     "${IDF_PATH}/components/esp_timer/include" 
                     "${IDF_PATH}/components/log/include"         
                     PRIV_REQUIRES spi_flash esp_psram esp_wifi esp_netif esp_event esp_http_server nvs_flash lwip mdns)