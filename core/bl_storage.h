#ifndef BL_STORAGE_H
#define BL_STORAGE_H
/* APP 区升级操作（partition.md §2 地址校验矩阵强制在此层）：擦/写/校验 */
#include <stdint.h>
#include "bl_protocol.h"   /* bl_status_t */

bool bl_storage_init(void);
bl_status_t bl_storage_erase_app(void);   /* 整片 APP 区，页间喂狗 */
bl_status_t bl_storage_write_chunk(uint32_t offset, const uint8_t *data, uint32_t len);
/* 纯校验（不落盘，ADR-020 拆分）：范围检查 + CRC32；sha256_out 非 NULL 时对同一
   读透同步计算 SHA-256（签名验签用）。calc_crc/calc_size 供响应 */
bl_status_t bl_storage_check_app(uint32_t size, uint32_t expect_crc,
                                 uint32_t *calc_crc, uint32_t *calc_size,
                                 uint8_t *sha256_out);
/* 持久化：auth=false = legacy VERIFY（auth=0），true = VERIFY_SIGNED（auth=1）；
   幂等跳过判定分别用 matches_app / matches_app_signed */
bl_status_t bl_storage_persist_app(uint32_t size, uint32_t crc32, bool auth);
/* 组合：校验并（成功时）持久化（legacy 路径） */
bl_status_t bl_storage_verify_app(uint32_t size, uint32_t expect_crc,
                                  uint32_t *calc_crc, uint32_t *calc_size);

#endif /* BL_STORAGE_H */
