#ifndef __ONENET_MQTT_H__
#define __ONENET_MQTT_H__
#include "esp_err.h"

//产品ID、设备名称、设备密钥
#define ONENET_PRODUCT_ID "8x5w9DD3Av"
#define ONENET_PRODUCT_ACCESS_KEY "UycLzekhG0HPmcMDzS03H/PB8QtHzRX9jdk/h4EdUxk="
#define ONENET_DEVICE_NAME "device_A"

esp_err_t onenet_start(void);

// esp_err_t onenet_post_property_data(const char* data);

#endif
