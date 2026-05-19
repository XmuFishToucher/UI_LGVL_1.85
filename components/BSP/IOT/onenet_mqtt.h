#ifndef __ONENET_MQTT_H__
#define __ONENET_MQTT_H__
#include "esp_err.h"
#include <stdint.h>

//产品ID、设备名称、设备密钥
#define ONENET_PRODUCT_ID "8x5w9DD3Av"
#define ONENET_PRODUCT_ACCESS_KEY "UycLzekhG0HPmcMDzS03H/PB8QtHzRX9jdk/h4EdUxk="
#define ONENET_DEVICE_NAME "device_A"

// 自定义 Topic: Device A → Device B 直通
#define CUSTOM_TOPIC "8x5w9DD3Av/interaction"

// 传感器最大值结构体
typedef struct {
    uint8_t  channel;    // 0-46
    uint16_t intensity;  // 校准后的值
} sensor_peak_t;

// 启动 OneNET MQTT 连接
esp_err_t onenet_start(void);

// 找 47 通道中的最大值
sensor_peak_t find_max_channel(const uint16_t *data);

// 找最大值 + 全零过滤 + 100ms 限流 + MQTT 发布 (在 uart_task 中调用)
void sensor_publish_max_channel(const uint16_t *data);

#endif
