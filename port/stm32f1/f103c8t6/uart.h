#ifndef BL_UART_H
#define BL_UART_H
/* USART1 PA9/PA10（protocol.md §3）：RX 中断 + 环形缓冲，TX 阻塞 */
#include <stdint.h>
#include <stdbool.h>

void bl_uart_port_init(void);          /* 按 BL_UART_BAUD 初始化 */
void bl_uart_port_deinit(void);        /* 九步跳转第 6 步 */
uint32_t bl_uart_port_rx_pop(uint8_t *buf, uint32_t max);  /* 非阻塞取环形缓冲 */
uint32_t bl_uart_port_rx_total(void);  /* 累计收到字节数（接线诊断用） */

#endif /* BL_UART_H */
