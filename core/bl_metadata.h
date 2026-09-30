#ifndef BL_METADATA_H
#define BL_METADATA_H
/* 参数区双副本（partition.md §4-§8）：所有写路径均为掉电安全写 */
#include <stdint.h>
#include <stdbool.h>

#define BL_META_FLAG_BL_REQUEST 0x00000001u

typedef struct {
    uint32_t seq;        /* 最新有效副本序号，出厂态 0 */
    uint32_t app_size;   /* 0 = 无有效 APP */
    uint32_t app_crc32;
    uint32_t flags;      /* bit0 = bl_request */
    uint16_t app_ver_major;
    uint16_t app_ver_minor;
    uint16_t app_ver_patch;
    uint8_t active_copy; /* 0=A(页62) 1=B(页63) 0xFF=无有效副本 */
    uint8_t app_auth;    /* 副本 0x24 字节：1 = 镜像已通过签名验证（0.4.0，ADR-020；
                            旧副本该处 0xFF → 0，fail-safe） */
    uint16_t dev_id;     /* 副本 0x25-0x26 字节：写这条记录的 BL 的芯片身份
                            （board_config BL_CHIP_DEVID = DBGMCU DEV_ID[11:0]；
                            0.5.0，ADR-021）。0xFFFF = 未记录（0.5.0 前的旧记录或出厂态）——
                            与 auth 同理不参与有效性判定，旧固件读到它也只是忽略 */
} bl_meta_t;

bool bl_meta_load(bl_meta_t *out);                       /* §6.1 读取规则 */
bool bl_meta_commit_app(uint32_t size, uint32_t crc32);  /* legacy VERIFY 持久化（auth=0） */
bool bl_meta_commit_app_signed(uint32_t size, uint32_t crc32); /* VERIFY_SIGNED 持久化（auth=1） */
bool bl_meta_matches_app(uint32_t size, uint32_t crc32); /* 已持久化相同内容且无待消费 bl_request */
bool bl_meta_matches_app_signed(uint32_t size, uint32_t crc32); /* 上者 + auth=1（幂等跳过判定） */
bool bl_meta_set_bl_request(bool set);                   /* 置位/清除 */
bool bl_meta_set_app_version(uint16_t ma, uint16_t mi, uint16_t pa);

#endif /* BL_METADATA_H */
