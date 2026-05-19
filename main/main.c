#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "ST77916.h"
#include "lvgl_ui.h"
#include "ui_matrix.h"

#include "wifi_manager.h"
#include "onenet_mqtt.h"
#include "nvs_flash.h"

static const char *TAG = "MAIN";

#define TOTAL_POINTS    47

static uint16_t g_matrix_data[TOTAL_POINTS];

// MQTT 收到 Device A 数据后，单点更新矩阵 (从 onenet_mqtt.c 回调)
void matrix_update_from_mqtt(uint8_t ch, uint16_t val)
{
    if (ch >= TOTAL_POINTS) return;
    g_matrix_data[ch] = val;
    lvgl_port_lock(0);
    ui_matrix_update(g_matrix_data);
    lvgl_port_unlock();
}

#define ssid "eeg"
#define password "zhangxu123"

static EventGroupHandle_t wifi_ev = NULL;
#define WIFI_CONNECTED_BIT BIT0

static void wifi_state_cb(WIFI_STATE state)
{
    switch (state) {
    case WIFI_STATE_DISCONNECTED:
        ESP_LOGI("wifi_state_cb", "WIFI_STATE_DISCONNECTED");
        break;
    case WIFI_STATE_CONNECTED:
        ESP_LOGI("wifi_state_cb", "WIFI_STATE_CONNECTED");
        xEventGroupSetBits(wifi_ev, WIFI_CONNECTED_BIT);
        break;
    default:
        break;
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing BSP...");
    LCD_Init();

    ESP_LOGI(TAG, "Initializing LVGL UI...");
    ret = lvgl_ui_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL UI initialization failed: %d", ret);
        return;
    }

    wifi_ev = xEventGroupCreate();
    wifi_manager_init(wifi_state_cb);
    wifi_manager_connect(ssid, password);
    EventBits_t ev;
    ev = xEventGroupWaitBits(wifi_ev, WIFI_CONNECTED_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    if (ev & WIFI_CONNECTED_BIT)
    {
        onenet_start();
    }

    ESP_LOGI(TAG, "Device B ready, waiting for MQTT data...\n");
}
