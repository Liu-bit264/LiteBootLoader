#include "clock.h"
#include "systick.h"
#include "board_config.h"
#include "bl_port.h"
#include "stm32f10x.h"

static uint32_t s_sysclk_hz = 8000000u;
static bool s_hse_ok = false;

/* CMSIS 全局变量定义（原 system_stm32f10x.c 职责，本工程由 clock.c 承担） */
uint32_t SystemCoreClock = 8000000u;

/* SystemInit：由启动文件在散布初始化前调用。只操作寄存器，不写静态变量
   （scatter 清零发生在 __main，会覆盖此阶段的全局写）。
   时钟源由 BL_USE_HSE 决定（ADR-010）：0=HSI 8MHz 直驱（频率由厂商保证，
   不依赖晶振——手焊板晶振频率未验证时的安全默认）；1=HSE 8M->PLL 72M。 */
void SystemInit(void)
{
#if BL_USE_HSE
    /* FLASH 等待周期：72MHz 需 2WS，预取开启 */
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    RCC->CR |= RCC_CR_HSEON;
    uint32_t timeout = 600000u; /* ~300ms@8MHz：插件晶振起振可能偏慢；IWDG 此时尚未开启 */
    while (!(RCC->CR & RCC_CR_HSERDY) && --timeout) {
    }

    if (RCC->CR & RCC_CR_HSERDY) {
        s_hse_ok = true;
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_PLLMULL) | RCC_CFGR_PLLMULL9 |
                    RCC_CFGR_PLLSRC;
        RCC->CR |= RCC_CR_PLLON;
        timeout = 120000u;
        while (!(RCC->CR & RCC_CR_PLLRDY) && --timeout) {
        }
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
        timeout = 120000u;
        while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL && --timeout) {
        }
        if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
            s_hse_ok = false; /* PLL 切换失败，保持 HSI */
        }
    } else {
        RCC->CR &= ~RCC_CR_HSEON;
        s_hse_ok = false;
    }
#else
    /* HSI 8MHz 直驱：0 等待周期，关掉 HSE/PLL 保证 SYSCLK 确为 HSI */
    FLASH->ACR = FLASH_ACR_PRFTBE;
    RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_PLLON);
    RCC->CFGR &= ~(RCC_CFGR_SW | RCC_CFGR_PLLMULL | RCC_CFGR_PLLSRC);
    s_hse_ok = false;
#endif
    /* 总线分频：AHB /1，APB1 /2（36MHz 上限），APB2 /1 */
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) |
                RCC_CFGR_PPRE1_DIV2;
}

void bl_clock_port_init(void)
{
    s_sysclk_hz = s_hse_ok ? 72000000u : 8000000u;
    SystemCoreClock = s_sysclk_hz; /* CMSIS 全局变量，供 SysTick 分频 */
}

uint32_t bl_clock_get_hz(void) { return s_sysclk_hz; }

bool bl_clock_hse_active(void) { return s_hse_ok; }

/* ---- bl_clock_ops ---- */
static void ops_init(void) { bl_clock_port_init(); }

static uint32_t ops_sysclk(void) { return s_sysclk_hz; }

static uint32_t ops_tick(void)
{
    return bl_systick_get_ms();
}

static void ops_delay(uint32_t ms)
{
    uint32_t start = bl_systick_get_ms();
    while ((bl_systick_get_ms() - start) < ms) {
        __NOP();
    }
}

const bl_clock_ops bl_clock = {
    .init = ops_init,
    .sysclk_hz = ops_sysclk,
    .tick_ms = ops_tick,
    .delay_ms = ops_delay,
};

/* ---- 跳转序列辅助（architecture.md §3） ---- */
void bl_port_disable_irq(void) { __disable_irq(); }

void bl_port_set_vtor(uint32_t addr) { SCB->VTOR = addr; }

void bl_port_set_msp(uint32_t value) { __set_MSP(value); }

void bl_port_jump(uint32_t reset_handler)
{
    void (*entry)(void) = (void (*)(void))(reset_handler & ~1u);
    entry();
}

void bl_port_system_reset(void) { NVIC_SystemReset(); }

void bl_port_clear_pending_irqs(void)
{
    for (uint32_t i = 0; i < 8u; i++) {
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }
}

uint32_t bl_port_read_word(uint32_t addr) { return *(const volatile uint32_t *)addr; }
