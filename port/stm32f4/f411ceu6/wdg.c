#include "wdg.h"
#include "bl_port.h"
#include "stm32f4xx.h"

/* F411 LSI 名义 32kHz（DS10697 允差宽）：prescaler=256 -> 125Hz，tick 8ms；
   reload = timeout_ms / 8。常规 2000ms -> 250、放宽 8000ms -> 1000，均 ≤4095。
   LSIPR/256 一档覆盖常规与放宽两档，无需换分频（与 F1 仅改 RLR 同构）。 */
void bl_wdg_port_init(uint32_t timeout_ms)
{
    IWDG->KR = 0xCCCCAu;                     /* 启动 LSI */
    IWDG->KR = 0x5555Au;                     /* 解锁寄存器 */
    IWDG->PR = IWDG_PR_PR_2 | IWDG_PR_PR_1 | IWDG_PR_PR_0;  /* PSC = 256 */
    IWDG->RLR = (timeout_ms * 125u + 500u) / 1000u; /* 四舍五入到 tick */
    IWDG->KR = 0xAAAAu;                      /* 重载并等待 LSI 稳定 */
    while (IWDG->SR & IWDG_SR_RVU) {
    }
    while (IWDG->SR & IWDG_SR_PVU) {
    }
}

void bl_wdg_port_refresh(void) { IWDG->KR = 0xAAAAu; }

/* ---- bl_wdg_ops ---- */
static void ops_init(uint32_t timeout_ms) { bl_wdg_port_init(timeout_ms); }
static void ops_refresh(void) { bl_wdg_port_refresh(); }

/* 运行时重配（ADR-015 升级期放宽）：仅改 RLR（PR 保持 /256）。
   F4 头号设计点：128K 扇区擦除 ~875ms（最大可翻倍）期间 CPU 停顿无法喂狗，
   升级窗口放宽到 8s（reload 1000 ≤ 4095）；LSI 实频偏移下窗口 5.4~15s，仍充分覆盖。 */
static bool ops_set_timeout(uint32_t timeout_ms)
{
    if (timeout_ms == 0u) {
        return false;
    }
    uint32_t reload = (timeout_ms * 125u + 500u) / 1000u;
    if (reload > 4095u) {
        return false;
    }
    /* 写 RLR 前等 RVU 清零（RM0390 IWDG_SR；审计 2026-09-29 P3-3）：
       RVU 在途（上一次 0xAAAA 重载未完成）时本次写入被硬件忽略——
       F4 放宽档是安全关键路径，必须确认写入真实生效 */
    while (IWDG->SR & IWDG_SR_RVU) {
    }
    IWDG->KR = 0x5555Au;                     /* 解锁 */
    IWDG->RLR = reload;
    IWDG->KR = 0xAAAAu;                      /* 重载并等同步 */
    while (IWDG->SR & IWDG_SR_RVU) {
    }
    return true;
}

const bl_wdg_ops bl_wdg = {
    .init = ops_init,
    .set_timeout_ms = ops_set_timeout,
    .refresh = ops_refresh,
};
