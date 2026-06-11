#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "ST77916.h"
#include "lvgl_ui.h"
#include "ui_matrix.h"
#include "uart.h"

#include "wifi_manager.h"
#include "onenet_mqtt.h"
#include "nvs_flash.h"

static const char *TAG = "MAIN";

#define TOTAL_POINTS    47
#define MATRIX_FRAME_HEADER_0 0xAA
#define MATRIX_FRAME_HEADER_1 0x55
#define MATRIX_FRAME_LEN (2 + TOTAL_POINTS * 2 + 1 + 2)
#define UART_PARSE_BUF_SIZE 256

static uint16_t local_matrix_data[TOTAL_POINTS];
static uint16_t local_matrix_raw[TOTAL_POINTS];
static uint16_t remote_matrix_data[TOTAL_POINTS];
static uint8_t local_uart_rx_buf[128];
static uint8_t local_uart_parse_buf[UART_PARSE_BUF_SIZE];
static uint16_t local_uart_parsed[TOTAL_POINTS];
static volatile bool stim_enabled = false;

static void send_stim_zero_frame(void)
{
    uint8_t tx_buf[MATRIX_FRAME_LEN] = {0};

    tx_buf[0] = MATRIX_FRAME_HEADER_0;
    tx_buf[1] = MATRIX_FRAME_HEADER_1;
    tx_buf[MATRIX_FRAME_LEN - 2] = 0x0D;
    tx_buf[MATRIX_FRAME_LEN - 1] = 0x0A;

    int written = uart_send_data(tx_buf, sizeof(tx_buf));
    if (written != (int)sizeof(tx_buf)) {
        ESP_LOGW(TAG, "Stim zero frame incomplete: written=%d expected=%d", written, (int)sizeof(tx_buf));
    }
}

bool app_stim_is_enabled(void)
{
    return stim_enabled;
}

void app_stim_set_enabled(bool enabled)
{
    stim_enabled = enabled;
    ESP_LOGI(TAG, "Stim %s", enabled ? "enabled" : "disabled");

    if (!enabled) {
        send_stim_zero_frame();
    }
}

static bool parse_matrix_frame(const uint8_t *frame, uint16_t *out)
{
    if (frame[0] != MATRIX_FRAME_HEADER_0 || frame[1] != MATRIX_FRAME_HEADER_1) {
        return false;
    }

    if (frame[MATRIX_FRAME_LEN - 2] != 0x0D || frame[MATRIX_FRAME_LEN - 1] != 0x0A) {
        return false;
    }

    uint8_t sum = 0;
    for (int i = 0; i < TOTAL_POINTS * 2; i++) {
        sum += frame[2 + i];
    }

    if (sum != frame[2 + TOTAL_POINTS * 2]) {
        return false;
    }

    for (int i = 0; i < TOTAL_POINTS; i++) {
        out[i] = (uint16_t)frame[2 + i * 2] | ((uint16_t)frame[2 + i * 2 + 1] << 8);
    }

    return true;
}

static void update_local_matrix_from_uart(const uint16_t *data)
{
    memcpy(local_matrix_raw, data, sizeof(local_matrix_raw));
    uart_update_latest_data(local_matrix_raw);

    memcpy(local_matrix_data, data, sizeof(local_matrix_data));
    uart_apply_zero(local_matrix_data);

    if (lvgl_port_lock(20)) {
        ui_matrix_update_local(local_matrix_data);
        lvgl_port_unlock();
    }
}

void app_zero_calibrate(void)
{
    uart_zero_calibrate();
    memcpy(local_matrix_data, local_matrix_raw, sizeof(local_matrix_data));
    uart_apply_zero(local_matrix_data);

    if (lvgl_port_lock(20)) {
        ui_matrix_update_local(local_matrix_data);
        lvgl_port_unlock();
    }
}

static void local_uart_rx_task(void *arg)
{
    size_t used = 0;

    while (1) {
        int len = uart_recv_data(local_uart_rx_buf, sizeof(local_uart_rx_buf), 20);
        if (len <= 0) {
            continue;
        }

        if (used + len > sizeof(local_uart_parse_buf)) {
            used = 0;
        }

        memcpy(&local_uart_parse_buf[used], local_uart_rx_buf, len);
        used += len;

        while (used >= 2) {
            size_t start = 0;
            while (start + 1 < used &&
                   !(local_uart_parse_buf[start] == MATRIX_FRAME_HEADER_0 && local_uart_parse_buf[start + 1] == MATRIX_FRAME_HEADER_1)) {
                start++;
            }

            if (start > 0) {
                memmove(local_uart_parse_buf, &local_uart_parse_buf[start], used - start);
                used -= start;
            }

            if (used < MATRIX_FRAME_LEN) {
                break;
            }

            if (parse_matrix_frame(local_uart_parse_buf, local_uart_parsed)) {
                update_local_matrix_from_uart(local_uart_parsed);
                memmove(local_uart_parse_buf, &local_uart_parse_buf[MATRIX_FRAME_LEN], used - MATRIX_FRAME_LEN);
                used -= MATRIX_FRAME_LEN;
            } else {
                memmove(local_uart_parse_buf, &local_uart_parse_buf[1], used - 1);
                used--;
            }
        }
    }
}

// MQTT 收到 Device A 数据后，单点更新矩阵 (从 onenet_mqtt.c 回调)
void matrix_update_from_mqtt(uint8_t ch, uint16_t val)
{
    if (ch >= TOTAL_POINTS) return;
    if (val == 0) {
        matrix_clear_from_mqtt();
        return;
    }

    remote_matrix_data[ch] = val;
    if (lvgl_port_lock(20)) {
        ui_matrix_update_remote(remote_matrix_data);
        lvgl_port_unlock();
    }
}

void matrix_update_all_from_mqtt(const uint16_t *data)
{
    if (!data) return;

    memcpy(remote_matrix_data, data, sizeof(remote_matrix_data));
    if (lvgl_port_lock(20)) {
        ui_matrix_update_remote(remote_matrix_data);
        lvgl_port_unlock();
    }
}

void matrix_clear_from_mqtt(void)
{
    memset(remote_matrix_data, 0, sizeof(remote_matrix_data));
    if (lvgl_port_lock(20)) {
        ui_matrix_clear_remote();
        lvgl_port_unlock();
    }
}

#define ssid "RMYiPhone"//eeg
#define password "Rmy20020123"//zhangxu123

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

    ESP_LOGI(TAG, "Initializing UART output...");
    uart_init_custom();
    app_stim_set_enabled(false);
    xTaskCreate(local_uart_rx_task, "local_uart_rx", 8192, NULL, 5, NULL);

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
