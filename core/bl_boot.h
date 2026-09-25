#ifndef BL_BOOT_H
#define BL_BOOT_H
/* 启动决策与跳转（design.md ADR-004，AGENTS.md §5.1 九步） */
#include <stdint.h>
#include <stdbool.h>

bool bl_boot_app_valid(void);   /* 缓存元数据 + 向量表检查 + 实时 CRC 重算 */
void bl_boot_jump(void);        /* 执行九步的第 4-9 步；调用前须完成 UI 停止与喂狗；不返回 */

#endif /* BL_BOOT_H */
