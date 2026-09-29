/* main：初始化顺序见 architecture.md §4/§5（IWDG 最先开启）。
   显示/调试为可选服务（实验分支）：main 只做强制链路（wdg/clock/systick/
   uart/gpio）+ core 装配；OLED/I2C 自举由 services/display_oled 的 init 承担。 */
#include "board_config.h"
#include "bl_port.h"
#include "clock.h"
#include "systick.h"
#include "uart.h"
#include "gpio.h"
#include "wdg.h"
#include "bl_core.h"

int main(void)
{
    bl_wdg.init(BL_IWDG_TIMEOUT_MS);   /* 喂狗点：BL 启动即开，永不关（ADR-011） */
    bl_clock.init();
    bl_systick_init();
    bl_uart.init();
    bl_gpio.init();
    bl_core_init();
    bl_core_run();                     /* 不返回 */
    return 0;
}
