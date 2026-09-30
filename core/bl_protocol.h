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
    BL_STATUS_SIGN_ERROR = 0x06,   /* 签名验签失败（0.4.0，ADR-020） */
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

/* OTA 扩展（protocol.md §5.1，规划书目标 3）：0x10 已实现；0x12–0x1F 继续预留 */
#define BL_CMD_OTA_QUERY   0x10u

/* 签名验签（protocol.md §5.11，ADR-020）：仅 BL_SIGN_EN=1 的支持包实现 */
#define BL_CMD_VERIFY_SIGNED 0x11u

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
uint32_t bl_protocol_stat_crc_fail(void);       /* CRC 不符被静默丢弃的帧 */
uint32_t bl_protocol_stat_byte_timeout(void);   /* 帧内超时复位半帧的次数 */
uint32_t bl_protocol_stat_timeout_pending(void);/* 超时时环形缓冲仍有字节的次数 */
void bl_protocol_stat_timeout_detail(uint32_t out[4]); /* 最近超时现场 [gap,pending,state,got] */

#endif /* BL_PROTOCOL_H */
