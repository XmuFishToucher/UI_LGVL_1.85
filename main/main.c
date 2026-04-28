#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "ST77916.h"
#include "lvgl_ui.h"
#include "uart.h"
#include "ui_matrix.h"

static const char *TAG = "MAIN";

#define TOTAL_POINTS    47
#define FRAME_HEADER_1  0xAA
#define FRAME_HEADER_2  0x55
#define FRAME_SIZE      (2 + TOTAL_POINTS*2 + 1 + 2)   // header + data + checksum + \r\n

static uint8_t rx_buf[128];
static int rx_len = 0;

uint16_t matrix_data[TOTAL_POINTS];

void uart_task(void *arg)
{
    uint8_t data[64];

    while (1)
    {
        int len = uart_recv_data(data, sizeof(data), 100);

        if (len > 0)
        {
            memcpy(rx_buf + rx_len, data, len);
            rx_len += len;

            if (rx_len > sizeof(rx_buf))
                rx_len = 0;

            // 查找帧
            for (int i = 0; i < rx_len - FRAME_SIZE; i++)
            {
                if (rx_buf[i] == FRAME_HEADER_1 && rx_buf[i+1] == FRAME_HEADER_2)
                {
                    uint8_t *frame = &rx_buf[i];

                    // 解析 TOTAL_POINTS 点
                    for (int j = 0; j < TOTAL_POINTS; j++)
                    {
                        uint8_t low  = frame[2 + j*2];
                        uint8_t high = frame[3 + j*2];
                        matrix_data[j] = (high << 8) | low;
                    }

                    // 打印验证
                    printf("Matrix (%d pts):\n", TOTAL_POINTS);
                    for (int j = 0; j < TOTAL_POINTS; j++)
                    {
                        printf("%4d ", matrix_data[j]);
                        if ((j + 1) % 11 == 0)
                            printf("\n");
                    }
                    printf("\n\n");

                    // 更新UI
                    lvgl_port_lock(0);
                    ui_matrix_update(matrix_data);
                    lvgl_port_unlock();

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
    ESP_LOGI(TAG, "Initializing BSP...");
    LCD_Init();

    ESP_LOGI(TAG, "Initializing LVGL UI...");
    esp_err_t ret = lvgl_ui_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL UI initialization failed: %d", ret);
        return;
    }

    ESP_LOGI(TAG, "UART START (47-point mode)\n");

    uart_init_custom();

    xTaskCreate(uart_task, "uart_task", 4096, NULL, 5, NULL);
}
