#ifndef BL_CORE_H
#define BL_CORE_H
/* 总状态机与命令分发（architecture.md §5）；协议帧经 bl_protocol 到达此处 */
#include <stdint.h>

void bl_core_init(void);
void bl_core_run(void);   /* 主循环，不返回 */
void bl_core_frame_received(uint8_t cmd, uint8_t seq, const uint8_t *data, uint32_t len);

#endif /* BL_CORE_H */
