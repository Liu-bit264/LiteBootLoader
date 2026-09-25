#ifndef BL_SYSTICK_H
#define BL_SYSTICK_H
/* SysTick 1ms 节拍（architecture.md §6）：只递增计数，不做业务 */
#include <stdint.h>

void bl_systick_init(void);            /* 按 SystemCoreClock 配置 1ms 中断 */
void bl_systick_stop(void);            /* 九步跳转第 5 步 */
uint32_t bl_systick_get_ms(void);

#endif /* BL_SYSTICK_H */
