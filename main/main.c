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
#include "uart.h"
#include "ui_matrix.h"

#include "wifi_manager.h"
#include "onenet_mqtt.h"
#include "nvs_flash.h"

static const char *TAG = "MAIN";

#define TOTAL_POINTS    47
#define FRAME_HEADER_1  0xAA
#define FRAME_HEADER_2  0x55
#define FRAME_SIZE      (2 + TOTAL_POINTS*2 + 1 + 2)   // header + data + checksum + \r\n

static uint8_t rx_buf[256];
static int rx_len = 0;

static uint16_t matrix_data[TOTAL_POINTS];

#define ssid "RMYiPhone"
#define password "Rmy20020123"

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

void uart_task(void *arg)
{
    uint8_t data[64];

    while (1)
    {
        int len = uart_recv_data(data, sizeof(data), 100);

        if (len > 0)
        {
            // 先检查边界，防止缓冲区溢出
            if (rx_len + len > sizeof(rx_buf))
            {
                // 丢弃旧数据为新数据腾空间
                int overflow = rx_len + len - sizeof(rx_buf);
                if (overflow < rx_len)
                {
                    memmove(rx_buf, rx_buf + overflow, rx_len - overflow);
                    rx_len -= overflow;
                }
                else
                {
                    rx_len = 0;
                }
            }

            // 边界安全之后再拷贝
            int copy_len = (rx_len + len <= sizeof(rx_buf)) ? len : sizeof(rx_buf) - rx_len;
            memcpy(rx_buf + rx_len, data, copy_len);
            rx_len += copy_len;

            // 查找帧
            for (int i = 0; i < rx_len - FRAME_SIZE; i++)
            {
                if (rx_buf[i] == FRAME_HEADER_1 && rx_buf[i+1] == FRAME_HEADER_2)
                {
                    uint8_t *frame = &rx_buf[i];

                    // 在 LVGL 锁内解析并更新 UI，避免数据竞争
                    lvgl_port_lock(0);
                    for (int j = 0; j < TOTAL_POINTS; j++)
                    {
                        uint8_t low  = frame[2 + j*2];
                        uint8_t high = frame[3 + j*2];
                        matrix_data[j] = (high << 8) | low;
                    }
                    uart_update_latest_data(matrix_data);
                    uart_apply_zero(matrix_data);
                    ui_matrix_update(matrix_data);
                    lvgl_port_unlock();

                    // MQTT 发布最大值通道和强度 (有信号限流发送，结束时清零一次)
                    sensor_publish_max_channel(matrix_data);

                    // // 打印验证
                    // ESP_LOGI(TAG, "Matrix (%d pts):", TOTAL_POINTS);
                    // for (int j = 0; j < TOTAL_POINTS; j++)
                    // {
                    //     printf("%4d ", matrix_data[j]);
                    //     if ((j + 1) % 11 == 0)
                    //         printf("\n");
                    // }
                    // printf("\n");

                    // 移除已处理数据
                    memmove(rx_buf, rx_buf + i + FRAME_SIZE, rx_len - (i + FRAME_SIZE));
                    rx_len -= (i + FRAME_SIZE);

                    break;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
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
    wifi_ev = xEventGroupCreate();
    wifi_manager_init(wifi_state_cb);
    wifi_manager_connect(ssid, password);
    EventBits_t ev;
    ev = xEventGroupWaitBits(wifi_ev, WIFI_CONNECTED_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    if(ev & WIFI_CONNECTED_BIT)
    {
        onenet_start();
    }

    ESP_LOGI(TAG, "Initializing BSP...");
    LCD_Init();

    ESP_LOGI(TAG, "Initializing LVGL UI...");
    ret = lvgl_ui_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL UI initialization failed: %d", ret);
        return;
    }

    ESP_LOGI(TAG, "UART START (47-point mode)\n");

    uart_init_custom();

    BaseType_t task_ret = xTaskCreate(uart_task, "uart_task", 8192, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UART task: %d", task_ret);
        return;
    }
}
