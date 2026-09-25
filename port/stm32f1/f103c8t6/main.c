/* main：初始化顺序见 architecture.md §4/§5（IWDG 最先开启） */
#include "board_config.h"
#include "bl_port.h"
#include "clock.h"
#include "systick.h"
#include "uart.h"
#include "gpio.h"
#include "i2c.h"
#include "wdg.h"
#include "ssd1306.h"
#include "bl_core.h"

int main(void)
{
    bl_wdg.init(BL_IWDG_TIMEOUT_MS);   /* 喂狗点：BL 启动即开，永不关（ADR-011） */
    bl_clock.init();
    bl_systick_init();
    bl_uart.init();
    bl_gpio.init();
    bl_i2c.init();
    ssd1306_init();
    bl_core_init();
    bl_core_run();                     /* 不返回 */
    return 0;
}
