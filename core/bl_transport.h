#ifndef BL_TRANSPORT_H
#define BL_TRANSPORT_H
/* transport 抽象（external_interface.md §3.1）：首实现 UART，OTA 等通道扩展点 */
#include <stdint.h>
#include <stdbool.h>

void bl_transport_init(void);
bool bl_transport_send(const uint8_t *buf, uint32_t len);
uint32_t bl_transport_recv(uint8_t *buf, uint32_t max);   /* 非阻塞，返回实际字节数 */

#endif /* BL_TRANSPORT_H */
