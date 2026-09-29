#ifndef BL_SYSTICK_H
#define BL_SYSTICK_H
/* SysTick 1ms 节拍（architecture.md §6）：只递增计数 + 可选用户钩子，不做其他业务 */
#include <stdint.h>

void bl_systick_init(void);            /* 按 SystemCoreClock 配置 1ms 中断 */
void bl_systick_stop(void);            /* 九步跳转第 5 步 */
uint32_t bl_systick_get_ms(void);
/* 用户 1ms 钩子：SysTick 中断内调用。弱定义为空，用户侧强符号覆盖。
 * 用途：把必须严格 1ms 节拍、不能被主循环阻塞的轻量操作放进来；禁止耗时操作。 */
void bl_systick_user_hook(void);

#endif /* BL_SYSTICK_H */
