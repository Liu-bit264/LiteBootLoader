/* 显示服务实现：OLED（SSD1306）页渲染 + PC13 LED 状态指示（ADR-014）。
 * 实现 core/bl_display.h 的统一 API；语义与时序沿用原 core/bl_ui.c（ADR-008）。 */
#include "bl_display.h"
#include "bl_port.h"
#include "board_config.h"
#include "bl_version.h"
#include "bl_protocol.h"
#include "ssd1306.h"
#include "clock.h"
#include "bl_transport.h"

static bl_display_state_t s_state;
static uint8_t  s_progress;
static bool     s_crc_ok;
static uint32_t s_next_refresh;

/* LED 模式（ADR-008）：返回 true = 亮（PC13 低有效，写引脚时取反） */
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
    s_progress = 0u;
    s_crc_ok = false;
    s_next_refresh = 0u;
    bl_gpio.write(BL_PIN_LED, true);   /* 高电平 = 灭 */
    /* 显示自举（实验：main 不再强制 i2c/ssd1306，服务自含） */
    bl_i2c.init();
    ssd1306_init();
    ssd1306_clear();
}

static void display_set_state(bl_display_state_t state) { s_state = state; }

static void display_set_progress(uint8_t percent) { s_progress = percent; }

static void display_set_crc_ok(bool ok) { s_crc_ok = ok; }

/* 无符号十进制定宽（右对齐补零），返回写入长度 */
static uint8_t fmt_dec(char *dst, uint32_t v, uint8_t width)
{
    char tmp[10];
    uint8_t n = 0;
    do {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u && n < width);
    uint8_t out = 0;
    for (uint8_t pad = n; pad < width; pad++) {
        dst[out++] = '0';
    }
    while (n) {
        dst[out++] = tmp[--n];
    }
    return out;
}

static void refresh_screen(void)
{
    char line[24];

    ssd1306_puts(0, 0, "LiteBL " BL_VERSION_STRING);
    ssd1306_puts(0, 1, bl_clock_hse_active() ? "F103C8  CLK:72M" : "F103C8  CLK:8M(HSI)");

    switch (s_state) {
    case BL_DISPLAY_UPGRADING:
        line[0] = '\0';
        {
            /* 最多 21 字符（128/6） */
            uint32_t i = 0;
            const char *p = "UPG:";
            while (*p) { line[i++] = *p++; }
            uint32_t v = s_progress;
            if (v >= 100u) { line[i++] = '1'; line[i++] = '0'; line[i++] = '0'; v = 0u; }
            else {
                if (v >= 10u) { line[i++] = (char)('0' + v / 10u); }
                line[i++] = (char)('0' + v % 10u);
            }
            line[i++] = '%';
            line[i] = '\0';
        }
        ssd1306_puts(0, 2, line);
        break;
    default:
        ssd1306_puts(0, 2, "MODE:WAIT/HOST");
        break;
    }

    ssd1306_puts(0, 4, s_crc_ok ? "CRC:OK " : "CRC:-- ");
    /* 蓝牙通道状态：STATE 引脚高 = SPP 已连接（bluetooth_notes.md §1） */
    ssd1306_puts(0, 5, bl_gpio.read(BL_PIN_BT_STATE) ? "BT:OK " : "BT:-- ");
    ssd1306_puts(8, 5, "WDG:ON ");

    /* 接线诊断行：RX=各通道累计收到字节，VF=已解析送达的有效帧（protocol.md §4.2） */
    {
        char diag[22];
        uint8_t k = 0;
        diag[k++] = 'R'; diag[k++] = 'X'; diag[k++] = ':';
        k += fmt_dec(diag + k, bl_transport_rx_total(), 7u);
        diag[k++] = ' '; diag[k++] = 'V'; diag[k++] = 'F'; diag[k++] = ':';
        (void)fmt_dec(diag + k, bl_protocol_stat_delivered(), 7u);
        diag[21] = '\0';
        ssd1306_puts(0, 6, diag);
    }
}

static void display_tick(uint32_t now_ms)
{
    /* LED：模式表驱动，非阻塞 */
    bl_gpio.write(BL_PIN_LED, !led_on(now_ms));

    /* OLED：限频 + 每次调用至多发一个页条带（分片间回主循环，architecture.md §8） */
    if ((int32_t)(now_ms - s_next_refresh) >= 0) {
        s_next_refresh = now_ms + BL_UI_REFRESH_MS;
#if BL_DISPLAY_USER_PAGE
        if (s_state == BL_DISPLAY_WAITING || s_state == BL_DISPLAY_UPGRADING) {
            bl_display_user_page();   /* 用户自检页：覆盖弱符号自行绘制 */
        } else {
            refresh_screen();
        }
#else
        refresh_screen();
#endif
    }
    (void)ssd1306_flush_strips();
}

/* 用户自检页面弱符号（AGENTS §8.1），默认空 */
__weak void bl_display_user_page(void) {}

/* ---- bl_display_ops（core/bl_display.h 统一 API） ---- */
const bl_display_ops bl_display = {
    .init = display_init,
    .set_state = display_set_state,
    .set_progress = display_set_progress,
    .set_crc_ok = display_set_crc_ok,
    .tick = display_tick,
};
