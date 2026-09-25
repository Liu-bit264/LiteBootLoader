#ifndef BL_STORAGE_H
#define BL_STORAGE_H
/* APP 区升级操作（partition.md §2 地址校验矩阵强制在此层）：擦/写/校验 */
#include <stdint.h>
#include "bl_protocol.h"   /* bl_status_t */

bool bl_storage_init(void);
bl_status_t bl_storage_erase_app(void);   /* 整片 APP 区，页间喂狗 */
bl_status_t bl_storage_write_chunk(uint32_t offset, const uint8_t *data, uint32_t len);
/* 校验并（成功时）持久化元数据；calc_crc/calc_size 供响应 */
bl_status_t bl_storage_verify_app(uint32_t size, uint32_t expect_crc,
                                  uint32_t *calc_crc, uint32_t *calc_size);

#endif /* BL_STORAGE_H */
