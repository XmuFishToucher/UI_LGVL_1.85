#include "uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void uart_init_custom(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    uart_param_config(USART_UX, &uart_config);
    uart_set_pin(USART_UX, USART_TX_GPIO_PIN, USART_RX_GPIO_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    uart_driver_install(USART_UX, 2048, 1024, 0, NULL, 0);
}


// 发送1字节
void uart_send_byte(uint8_t data)
{
    uart_write_bytes(USART_UX, (const char *)&data, 1);
}


// 发送数据
int uart_send_data(const uint8_t *data, uint16_t len)
{
    return uart_write_bytes(USART_UX, (const char *)data, len);
}


// 接收数据
int uart_recv_data(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms)
{
    return uart_read_bytes(USART_UX,
                           buf,
                           max_len,
                           pdMS_TO_TICKS(timeout_ms));
}

// ================ 调零（Zero Calibration）================
static float zero_offset[ZERO_CHANNELS] = {0};
static uint16_t latest_cap_data[ZERO_CHANNELS];

void uart_update_latest_data(uint16_t *data)
{
    memcpy(latest_cap_data, data, sizeof(latest_cap_data));
}

void uart_apply_zero(uint16_t *data)
{
    for (int i = 0; i < ZERO_CHANNELS; i++) {
        if (data[i] >= zero_offset[i]) {
            data[i] -= (uint16_t)zero_offset[i];
        } else {
            data[i] = 0;
        }
    }
}

// 调零：将当前 raw_value 记录为各通道零点偏移
void uart_zero_calibrate(void)
{
    for (int i = 0; i < ZERO_CHANNELS; i++) {
        zero_offset[i] = (float)latest_cap_data[i];
    }
    printf("Zero calibrated\r\n");
}