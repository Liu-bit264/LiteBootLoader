#include "systick.h"
#include "stm32f4xx.h"

static volatile uint32_t s_ms;

void bl_systick_init(void)
{
    s_ms = 0u;
    SysTick->LOAD = (SystemCoreClock / 1000u) - 1u;
    SysTick->VAL = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;
}

void bl_systick_stop(void)
{
    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL = 0u;
}

uint32_t bl_systick_get_ms(void) { return s_ms; }

__weak void bl_systick_user_hook(void) { /* 默认空：用户侧强符号覆盖 */ }

void SysTick_Handler(void)
{
    s_ms++;
    bl_systick_user_hook();
}

/* ---- bl_port 跳转辅助（architecture.md §3） ---- */
void bl_port_stop_systick(void) { bl_systick_stop(); }
