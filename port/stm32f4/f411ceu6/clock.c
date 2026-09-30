#include "clock.h"
#include "systick.h"
#include "board_config.h"
#include "bl_port.h"
#include "stm32f4xx.h"

/* 两个静态的初值只是 scatter 映像值；真实时钟状态在 bl_clock_port_init()
   里从硬件 SWS 位读回（SystemInit 阶段的静态写会被 __main 清零覆盖） */
static uint32_t s_sysclk_hz = 16000000u;
static bool s_hse_ok = false;

/* CMSIS 全局变量定义（system_stm32f4xx.c 职责，本工程由 clock.c 承担） */
uint32_t SystemCoreClock = 16000000u;

/* F411 PLL 参数（RM0383 §7.3：PLL 输入 1-2MHz 推荐 2MHz，VCO 输出 100-432MHz；
   目标 100MHz、VCO 200MHz、P=2。USB 未用，Q 值无实质影响取最小可用值）。
   核心板板载 25MHz HSE（另一颗 32.768 kHz 是 RTC 晶振，与 HSE 无关）；
   8MHz 分支仅供自换 8M 晶振的自制板，本仓未做板级验证。 */
#if BL_HSE_MHZ == 8
#define F411_PLL_M   4u    /* 8MHz/4  = 2MHz PLL 输入（自制板；未验证） */
#define F411_PLL_N   100u  /* 2MHz*100 = 200MHz VCO */
#define F411_PLL_P   2u
#define F411_PLL_Q   4u
#elif BL_HSE_MHZ == 25
#define F411_PLL_M   25u   /* 25MHz/25 = 1MHz PLL 输入（1MHz 为允许下限）——核心板标配 */
#define F411_PLL_N   200u  /* 1MHz*200 = 200MHz VCO */
#define F411_PLL_P   2u
#define F411_PLL_Q   10u
#else
#error "BL_HSE_MHZ 仅支持 25（核心板）或 8（自制板；未验证）——RM0383 PLL 输入 1-2MHz 约束下的整除组合"
#endif

/* SystemInit：由启动文件在散布初始化前调用。只操作寄存器，不写静态变量——
   scatter 清零发生在 __main，会覆盖此阶段的全局写（F1 阶段 1 排障结论同因）。
   运行时时钟状态由 bl_clock_port_init() 读 SWS 寄存器重新判定。
   复位路径（RM0383 §5/PWR）：先开 PWR 时钟并置 VOS=Scale 1（默认 Scale 2
   上限 84MHz，100MHz 必须前置），再 HSE->PLL 100MHz，失败回退 HSI 16MHz。 */
void SystemInit(void)
{
    /* 已运行在 PLL（BL 跳转进入 APP 的场景）：时钟已就绪，只需确保 3WS 与
       总线分频一致后直接返回。严禁落到低 WS——100MHz 下取指需 3WS。 */
    if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) {
        FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY_3WS;
        RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) |
                    RCC_CFGR_PPRE1_DIV2;   /* AHB 100M，APB1 50M，APB2 100M */
        return;
    }

    /* 复位路径：HSI 16MHz 安全起点（0 等待周期；切 100M 前才提升 WS） */
    FLASH->ACR = FLASH_ACR_PRFTEN;
    /* 100MHz 前置条件：PWR 时钟 + VOS=Scale 1（RM0383 §5.4.2） */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_VOS;
#if BL_USE_HSE
    RCC->CR |= RCC_CR_HSEON;
    uint32_t timeout = 600000u; /* ~300ms@16MHz：晶振起振可能偏慢；IWDG 此时尚未开启 */
    while (!(RCC->CR & RCC_CR_HSERDY) && --timeout) {
    }

    if (RCC->CR & RCC_CR_HSERDY) {
        /* PLLCFGR 一次写全 M/N/P/Q/PLLSRC（复位值 SWS 无关位全清对齐 ST 模板）；
           PLLP 字段 = P/2 - 1，P=2 -> 0 */
        RCC->PLLCFGR = F411_PLL_M | (F411_PLL_N << 6u) | (((F411_PLL_P >> 1u) - 1u) << 16u) |
                       RCC_PLLCFGR_PLLSRC_HSE | (F411_PLL_Q << 24u);
        RCC->CR |= RCC_CR_PLLON;
        timeout = 120000u;
        while (!(RCC->CR & RCC_CR_PLLRDY) && --timeout) {
        }
        /* 实跑 100M 之前才提升等待周期（DS10697：2.7-3.6V 下 100MHz = 3WS） */
        FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY_3WS;
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
        timeout = 120000u;
        while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL && --timeout) {
        }
        if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
            /* 切换失败：先回 HSI 再降等待周期（顺序不可反，高主频低 WS 会取指出错） */
            RCC->CFGR &= ~RCC_CFGR_SW;
            FLASH->ACR = FLASH_ACR_PRFTEN;
            RCC->CR &= ~RCC_CR_PLLON;
        }
    } else {
        RCC->CR &= ~RCC_CR_HSEON;
    }
#else
    /* HSI 16MHz 直驱：关掉 HSE/PLL 保证 SYSCLK 确为 HSI */
    RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_PLLON);
    RCC->CFGR &= ~(RCC_CFGR_SW | RCC_PLLCFGR_PLLSRC);
    FLASH->ACR = FLASH_ACR_PRFTEN;
#endif
    /* 总线分频：AHB /1（100MHz 上限），APB1 /2（50MHz 上限），APB2 /1（100MHz 上限） */
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) |
                RCC_CFGR_PPRE1_DIV2;
}

void bl_clock_port_init(void)
{
    /* 真值取自硬件 SWS 位：SystemInit 运行于 __main scatter 之前，其静态写
       会被初值覆盖，不能作为跨函数状态通道（F1 阶段 1 排障结论） */
    s_hse_ok = ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL);
    s_sysclk_hz = s_hse_ok ? 100000000u : 16000000u;
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

/* ---- 跳转序列辅助（architecture.md §3，与 F1 同构） ---- */
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
