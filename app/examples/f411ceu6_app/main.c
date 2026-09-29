/* main.c —— F411CEU6 最小 APP 示例（ADR-017）
 * 链接 0x08010000（linker/f411ceu6/app.sct）；由 BL 九步跳转进入。
 * 职责：VTOR 重定位、重新开中断（跳转第 4 步关闭）、接管 IWDG 喂狗、
 *       PC13 呼吸灯（与 BL 各类闪烁明显不同）、升级口最小响应器（请求回 BL）。
 * 与 f103c8t6_app 的差异：无 OLED/软 I2C——呼吸灯 PWM 即在 SysTick 中断内，
 * 主循环只做喂狗 + app_request_poll，几乎恒空闲。
 * 中断验证：SysTick 1ms（时基 + 呼吸灯 PWM）+ USART1 RX（响应器输入）均走中断。 */
#include <string.h>
#include "stm32f4xx.h"
#include "board_config.h"
#include "bl_port.h"
#include "clock.h"
#include "systick.h"
#include "uart.h"
#include "gpio.h"
#include "wdg.h"
#include "bl_metadata.h"
#include "app_request.h"

#define APP_VERSION_STRING "0.1.0"

static void app_print(const char *s)
{
    bl_uart.write((const uint8_t *)s, (uint32_t)strlen(s));
}

/* ---- 呼吸灯：SysTick 1ms 中断驱动（强符号覆盖 port 层弱定义） ----
 * 100Hz×10 槽软件 PWM；40 相位 ×100ms = 4s 呼吸周期（与 F103 示例同参）。 */
void bl_systick_user_hook(void)
{
    static uint32_t ms;
    uint32_t t = ms++;
    uint32_t slot = t % 10u;
    uint32_t phase = (t / 100u) % 40u;
    uint32_t bright = (phase < 20u) ? (phase / 2u) : ((39u - phase) / 2u);
    bl_gpio.write(BL_PIN_LED, slot >= bright);   /* 低电平点亮 */
}

int main(void)
{
    /* external_interface.md §5：启动设 VTOR（BL 跳转第 7 步已设，此处防御性重设） */
    SCB->VTOR = BL_APP_BASE;
    /* BL 九步跳转第 4 步关闭了全局中断：APP 重新打开（中断链路验证的前提） */
    __enable_irq();

    bl_wdg.refresh();      /* 跳转窗口：接管喂狗前先喂一口（IWDG 自 BL 起持续运行，只喂不配） */
    bl_clock.init();       /* 本镜像的 SystemCoreClock 独立于 BL，按硬件 SWS 重新派生 */
    bl_uart.init();        /* BL 第 6 步已 deinit，这里重建（USART1 RX 中断验证） */
    bl_gpio.init();
    /* SysTick 最后开：呼吸灯 PWM 在 SysTick 中断里写 LED，需 GPIO 先就绪 */
    bl_systick_init();     /* 1ms 节拍：呼吸灯时基 + SysTick 中断验证 */

    bl_meta_t meta;
    (void)bl_meta_load(&meta);   /* 装载参数区上下文（app_request 置位 bl_request 的前置） */
    app_request_init();

    app_print("\r\nA:F411 APP v" APP_VERSION_STRING " running, breathing\r\n");

    for (;;) {
        bl_wdg.refresh();   /* 喂狗点：主循环（接管 IWDG） */

        app_request_poll(); /* 升级口：SET_META(bl_request) → 落盘 → 复位回 BL */
    }
}
