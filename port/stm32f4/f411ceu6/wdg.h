#ifndef BL_WDG_H
#define BL_WDG_H
/* IWDG（ADR-011）：BL 启动即开启且永不关闭；约 2s（LSI 32kHz 名义值）；
   升级期放宽 8s（ADR-015/ADR-017，PR/256 一档覆盖两档） */
#include <stdint.h>

void bl_wdg_port_init(uint32_t timeout_ms);
void bl_wdg_port_refresh(void);

#endif /* BL_WDG_H */
