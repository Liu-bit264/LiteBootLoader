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

const bl_wdg_ops bl_wdg = {
    .init = ops_init,
    .refresh = ops_refresh,
};
