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
} bl_meta_t;

bool bl_meta_load(bl_meta_t *out);                       /* §6.1 读取规则 */
bool bl_meta_commit_app(uint32_t size, uint32_t crc32);  /* VERIFY 持久化 */
bool bl_meta_matches_app(uint32_t size, uint32_t crc32); /* 已持久化相同内容且无待消费 bl_request */
bool bl_meta_set_bl_request(bool set);                   /* 置位/清除 */
bool bl_meta_set_app_version(uint16_t ma, uint16_t mi, uint16_t pa);

#endif /* BL_METADATA_H */
