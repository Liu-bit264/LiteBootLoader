#ifndef BL_PROTOCOL_H
#define BL_PROTOCOL_H
/* 帧协议（protocol.md）：流式解析、组帧、协议活跃判定；命令分发在 bl_core */
#include <stdint.h>
#include <stdbool.h>

/* 状态码（protocol.md §5.0），全命令响应 DATA 首字节 */
typedef enum {
    BL_STATUS_OK = 0x00,
    BL_STATUS_CRC_ERROR = 0x01,
    BL_STATUS_FLASH_ERROR = 0x02,
    BL_STATUS_RANGE_ERROR = 0x03,
    BL_STATUS_STATE_ERROR = 0x04,
    BL_STATUS_TIMEOUT = 0x05,
} bl_status_t;

/* 命令编号（protocol.md §5.0） */
#define BL_CMD_PING        0x01u
#define BL_CMD_GET_INFO    0x02u
#define BL_CMD_ERASE_APP   0x03u
#define BL_CMD_WRITE_CHUNK 0x04u
#define BL_CMD_VERIFY_APP  0x05u
#define BL_CMD_SET_META    0x06u
#define BL_CMD_GET_META    0x07u
#define BL_CMD_JUMP_APP    0x08u
#define BL_CMD_RESET       0x09u

#define BL_PROTOCOL_VER    0x01u

void bl_protocol_init(void);
void bl_protocol_feed(const uint8_t *data, uint32_t len);       /* 主循环喂字节流 */
void bl_protocol_poll(uint32_t now_ms);                         /* 帧内 50ms 超时复位半帧 */
bool bl_protocol_send(uint8_t resp_cmd, uint8_t seq,
                      const uint8_t *data, uint32_t len);       /* 组帧发送（resp_cmd 已含 0x80） */
bool bl_protocol_is_active(uint32_t now_ms);                    /* BL_PROTOCOL_ACTIVE_MS 内有有效帧 */

/* 诊断计数（OLED 诊断行用）：CRC 通过的帧 / 已送达 core 的帧 */
uint32_t bl_protocol_stat_crc_ok(void);
uint32_t bl_protocol_stat_delivered(void);

#endif /* BL_PROTOCOL_H */
