#ifndef BL_UART2_H
#define BL_UART2_H
/* USART2 PA2/PA3（architecture.md §6.1 通道 1）：HC-05 蓝牙字节流，与 uart.c 同构 */
#include <stdint.h>

void bl_uart_bt_port_init(void);           /* 按 BL_BT_UART_BAUD 初始化 */
void bl_uart_bt_port_deinit(void);         /* 九步跳转第 6 步（随 bl_port_uart_deinit 调用） */
uint32_t bl_uart_bt_port_rx_pop(uint8_t *buf, uint32_t max);   /* 非阻塞取环形缓冲 */
uint32_t bl_uart_bt_port_rx_total(void);   /* 累计收到字节数（诊断） */
uint32_t bl_uart_bt_port_rx_pending(void); /* 环形缓冲待取字节数（诊断） */

#endif /* BL_UART2_H */
