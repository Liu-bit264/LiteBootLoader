#include "wdg.h"
#include "bl_port.h"
#include "stm32f10x.h"

/* LSI 名义 40kHz：prescaler=64 -> 625Hz；reload = timeout_ms * 0.625 */
void bl_wdg_port_init(uint32_t timeout_ms)
{
    IWDG->KR = 0xCCCCAu;                     /* 启动 LSI */
    IWDG->KR = 0x5555Au;                     /* 解锁寄存器 */
    IWDG->PR = IWDG_PR_PR_1 | IWDG_PR_PR_0;  /* PSC = 64 */
    IWDG->RLR = (timeout_ms * 625u + 500u) / 1000u; /* 四舍五入到 tick */
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

/* 运行时重配（ADR-015 升级期放宽）：仅改 RLR（PR 保持 /64）。
   F1 reload 上限 4095（12-bit）：本实现下 timeout ≤ 6552ms；更宽窗口
   （如 F4 的 8s）需按芯片改分频，移植时在各自 wdg.c 实现。 */
static bool ops_set_timeout(uint32_t timeout_ms)
{
    if (timeout_ms == 0u) {
        return false;
    }
    uint32_t reload = (timeout_ms * 625u + 500u) / 1000u;
    if (reload > 4095u) {
        return false;
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
