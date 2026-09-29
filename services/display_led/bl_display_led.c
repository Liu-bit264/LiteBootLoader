/* LED-only 显示服务实现（ADR-017）：无 OLED/无 I2C 的最小显示方案，
 * 实现 core/bl_display.h 统一 API；LED 模式表沿用 ADR-008 语义
 * （与 services/display_oled 同表，去掉屏幕渲染路径）。
 * 适用于：无 OLED 硬件的支持包（如 F411CEU6 最小实现包）。 */
#include "bl_display.h"
#include "bl_port.h"
#include "board_config.h"

static bl_display_state_t s_state;

/* LED 模式（ADR-008）：返回 true = 亮（低电平点亮，写引脚时取反） */
static bool led_on(uint32_t now)
{
    uint32_t m;
    switch (s_state) {
    case BL_DISPLAY_WAITING:
        return (now % 1000u) < 500u;
    case BL_DISPLAY_UPGRADING:
        return (now % 200u) < 100u;
    case BL_DISPLAY_APP_INVALID:
        m = now % 2000u;
        return (m < 200u) || (m >= 400u && m < 600u);
    case BL_DISPLAY_JUMPING:
        return true;
    case BL_DISPLAY_FAULT:
    default:
        m = now % 2000u;
        return (m < 100u) || ((m >= 200u) && (m < 300u)) ||
               ((m >= 400u) && (m < 500u));
    }
}

static void display_init(void)
{
    s_state = BL_DISPLAY_WAITING;
    bl_gpio.write(BL_PIN_LED, true);   /* 高电平 = 灭 */
}

static void display_set_state(bl_display_state_t state) { s_state = state; }

static void display_set_progress(uint8_t percent) { (void)percent; /* 无屏，仅 LED 模式 */ }

static void display_set_crc_ok(bool ok) { (void)ok; /* 无屏；CRC 结果经串口日志上报 */ }

static void display_tick(uint32_t now_ms)
{
    /* LED：模式表驱动，非阻塞（节拍由 BL_UI_REFRESH_MS 限频亦可，直驱更稳） */
    bl_gpio.write(BL_PIN_LED, !led_on(now_ms));
}

/* ---- bl_display_ops（core/bl_display.h 统一 API） ---- */
const bl_display_ops bl_display = {
    .init = display_init,
    .set_state = display_set_state,
    .set_progress = display_set_progress,
    .set_crc_ok = display_set_crc_ok,
    .tick = display_tick,
};
