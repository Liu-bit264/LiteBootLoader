#ifndef BL_CLOCK_H
#define BL_CLOCK_H
/* F103 时钟（ADR-010）：HSE 8MHz xPLL9=72MHz，HSE 失败回退 HSI 8MHz */
#include <stdint.h>
#include <stdbool.h>

void bl_clock_port_init(void);              /* main 阶段调用：派生 SystemCoreClock 等 */
uint32_t bl_clock_get_hz(void);             /* 当前 SYSCLK（含回退后实际值） */
bool bl_clock_hse_active(void);             /* 当前时钟源是否 HSE/PLL */

#endif /* BL_CLOCK_H */
