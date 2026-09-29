#ifndef BL_FLASH_H
#define BL_FLASH_H
/* F411 内置 Flash（partition.md §1/§2；RM0383 §3）：物理层，分区策略由 core/bl_storage 强制。
   擦除单元几何（ADR-015）统一经 bl_flash_ops 的 unit_* 接口暴露（非均匀扇区查表）。 */
#include <stdint.h>
#include <stdbool.h>

void bl_flash_lock(void);

#endif /* BL_FLASH_H */
