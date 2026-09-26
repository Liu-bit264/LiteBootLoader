/* main.c —— 阶段 2 APP 示例（AGENTS.md §12 阶段 2）
 * 链接 0x08004000（linker/app.sct）；由 BL 九步跳转进入，或上电经 BL 校验后自动跳转。
 * 职责：VTOR 重定位、重新开中断（跳转第 4 步关闭）、接管 IWDG 喂狗、
 *       PC13 呼吸灯（与 BL 各类闪烁明显不同）、OLED 状态显示、
 *       升级口最小响应器（请求回 BL）。
 * 中断验证：SysTick 1ms（呼吸灯时基）+ USART1 RX（响应器输入）均走中断。 */
#include <string.h>
#include "stm32f10x.h"
#include "board_config.h"
#include "bl_port.h"
#include "clock.h"
#include "systick.h"
#include "uart.h"
#include "gpio.h"
#include "i2c.h"
#include "wdg.h"
#include "ssd1306.h"
#include "bl_metadata.h"
#include "app_request.h"

#define APP_VERSION_STRING "0.1.0"

static void app_print(const char *s)
{
    bl_uart.write((const uint8_t *)s, (uint32_t)strlen(s));
}

/* ---- OLED：非阻塞、限频（architecture.md §8 原则，flush_strips 每次至多一页） ---- */

/* 无符号两位数（CLK:M 格式用） */
static void fmt_u2(char *dst, uint32_t v)
{
    dst[0] = (char)('0' + (v / 10u) % 10u);
    dst[1] = (char)('0' + v % 10u);
}

static void oled_init_page(void)
{
    char line[17];
    uint8_t i;
    const char *p;

    ssd1306_init();
    ssd1306_clear();

    p = "A:APP v" APP_VERSION_STRING;               /* A: 前缀与 BL 的 LiteBL 区分 */
    ssd1306_puts(0, 0, p);
    for (i = 0; i < 7u; i++) { line[i] = "F103C8 "[i]; }
    line[i++] = 'C'; line[i++] = 'L'; line[i++] = 'K'; line[i++] = ':';
    fmt_u2(&line[i], bl_clock.sysclk_hz() / 1000000u);
    line[i + 2] = 'M'; line[i + 3] = '\0';
    ssd1306_puts(0, 1, line);
    ssd1306_puts(0, 2, "RUN:breathing");
    ssd1306_puts(0, 4, "WDG:ON");                    /* IWDG 由 APP 接管持续喂 */
}

/* 呼吸亮度条（page 6，19 格）：内容变化才重画，未变化不产生脏页 */
static void oled_breath_bar(uint32_t bright)
{
    static uint32_t last = 0xFFFFFFFFu;
    char bar[23];
    uint32_t i;
    if (bright == last) {
        return;
    }
    last = bright;
    bar[0] = 'B';
    bar[1] = '[';
    for (i = 0; i < 19u; i++) {
        bar[2u + i] = (i < bright) ? '#' : '.';
    }
    bar[21] = ']';
    bar[22] = '\0';
    ssd1306_puts(0, 6, bar);
}

int main(void)
{
    /* external_interface.md §5：启动设 VTOR（BL 跳转第 7 步已设，此处防御性重设） */
    SCB->VTOR = BL_APP_BASE;
    /* BL 九步跳转第 4 步关闭了全局中断：APP 重新打开（中断链路验证的前提） */
    __enable_irq();

    bl_wdg.refresh();      /* 跳转窗口：接管喂狗前先喂一口（IWDG 自 BL 起持续运行，只喂不配） */
    bl_clock.init();       /* 本镜像的 SystemCoreClock 独立于 BL，按硬件 SWS 重新派生 */
    bl_systick_init();     /* 1ms 节拍：呼吸灯时基 + SysTick 中断验证 */
    bl_uart.init();        /* BL 第 6 步已 deinit，这里重建（USART1 RX 中断验证） */
    bl_gpio.init();
    bl_i2c.init();         /* PB8/PB9 软件 I2C：OLED 用（BL 第 6 步已释放总线） */
    ssd1306_init();
    ssd1306_clear();       /* 抹掉 BL 遗留画面，随后由 strip 刷新逐页送出 */

    bl_meta_t meta;
    (void)bl_meta_load(&meta);   /* 装载参数区上下文（app_request 置位 bl_request 的前置） */
    app_request_init();

    app_print("\r\nA:APP v" APP_VERSION_STRING " running, breathing\r\n");

    oled_init_page();
    oled_breath_bar(0u);

    uint32_t last_slot = 0xFFFFFFFFu;
    uint32_t last_phase = 0xFFFFFFFFu;
    for (;;) {
        bl_wdg.refresh();   /* 喂狗点：主循环（接管 IWDG，验收 §13.10） */

        app_request_poll(); /* 升级口：SET_META(bl_request) → 落盘 → 复位回 BL */

        /* PC13 呼吸灯（软件 PWM，100Hz×10 槽；100ms/级 × 40 级 = 4s 呼吸周期） */
        uint32_t t = bl_clock.tick_ms();
        uint32_t slot = t % 10u;
        if (slot != last_slot) {
            last_slot = slot;
            uint32_t phase = (t / 100u) % 40u;
            uint32_t bright = (phase < 20u) ? phase : (39u - phase);   /* 0..19 级 */
            bl_gpio.write(BL_PIN_LED, slot >= bright);   /* 低电平点亮 */
            if (phase != last_phase) {                   /* 100ms 一次：OLED 亮度条 */
                last_phase = phase;
                oled_breath_bar(bright);
            }
        }

        (void)ssd1306_flush_strips();   /* 非阻塞：每次至多发一个脏页，页间回主循环 */
    }
}
