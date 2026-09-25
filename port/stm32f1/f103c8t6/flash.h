#ifndef BL_FLASH_H
#define BL_FLASH_H
/* F103 内置 Flash（partition.md §1/§2）：物理层，分区策略由 core/bl_storage 强制 */
#include <stdint.h>
#include <stdbool.h>

uint32_t bl_flash_page_count(void);   /* 64 */
void bl_flash_lock(void);

#endif /* BL_FLASH_H */
