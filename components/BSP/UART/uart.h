#ifndef __UART_H__
#define __UART_H__

#include <string.h>
#include <stdint.h>
#include "driver/uart.h"
#include "driver/gpio.h"

#define USART_UX UART_NUM_2

#define USART_TX_GPIO_PIN GPIO_NUM_43
#define USART_RX_GPIO_PIN GPIO_NUM_44

#define RX_BUF_SIZE (1024)

// 初始化
void uart_init_custom(void);                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 

// 发送1字节
void uart_send_byte(uint8_t data);

// 发送缓冲区
int uart_send_data(const uint8_t *data, uint16_t len);

// 接收数据
int uart_recv_data(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms);

// ================ 调零（Zero Calibration）================
#define ZERO_CHANNELS 47

// 更新最新的原始数据（由 main.c 在每次接收到数据后调用，保存副本供调零使用）
void uart_update_latest_data(uint16_t *data);

// 对数据应用调零偏移（减去零点偏移量）
void uart_apply_zero(uint16_t *data);

// 调零：将当前最新数据记录为各通道的零点偏移
void uart_zero_calibrate(void);

#endif