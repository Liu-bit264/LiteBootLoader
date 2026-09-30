#ifndef BL_CLOCK_H
#define BL_CLOCK_H
/* F411CEU6 时钟（ADR-010/ADR-017）：HSE 25MHz（核心板晶振）->PLL 100MHz，HSE 失败回退
   HSI 16MHz；board_config.h 的 8MHz 分支仅供自换晶振的自制板（本仓未验证） */
#include <stdint.h>
#include <stdbool.h>

void bl_clock_port_init(void);              /* main 阶段调用：派生 SystemCoreClock 等 */
uint32_t bl_clock_get_hz(void);             /* 当前 SYSCLK（含回退后实际值） */
bool bl_clock_hse_active(void);             /* 当前时钟源是否 HSE/PLL */

#endif /* BL_CLOCK_H */
