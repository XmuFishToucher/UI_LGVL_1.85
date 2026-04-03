#ifndef __UART_H__
#define __UART_H__

#include <string.h>
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

#endif