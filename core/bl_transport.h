#ifndef BL_TRANSPORT_H
#define BL_TRANSPORT_H
/* transport 抽象（external_interface.md §3.1）：通道注册表，OTA 等通道扩展点。
 * 通道序号见 architecture.md §6.1：0=USART1 有线，1=UART2 蓝牙 HC-05，
 * WIFI 为占位 stub（规划书目标 2，未实现时被选路逻辑跳过）。 */
#include <stdint.h>
#include <stdbool.h>

#define BL_TRANSPORT_CH_WIRED  0u
#define BL_TRANSPORT_CH_BT     1u
#define BL_TRANSPORT_CH_NONE   0xFFu

void bl_transport_init(void);
bool bl_transport_send(const uint8_t *buf, uint32_t len);
uint32_t bl_transport_recv(uint8_t *buf, uint32_t max);   /* 非阻塞，返回实际字节数 */
uint8_t bl_transport_active_channel(void);                /* 活动通道序号；CH_NONE=无 */
uint32_t bl_transport_rx_total(void);                     /* 诊断：各通道累计接收字节之和 */
uint32_t bl_transport_rx_pending(void);                   /* 诊断：各通道环形缓冲待取字节之和 */

#endif /* BL_TRANSPORT_H */
