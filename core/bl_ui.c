#include "bl_ui.h"
#include "bl_port.h"
#include "board_config.h"
#include "bl_version.h"
#include "bl_protocol.h"
#include "ssd1306.h"
#include "clock.h"
#include "uart.h"

static bl_ui_state_t s_state;
static uint8_t  s_progress;
static bool     s_crc_ok;
static uint32_t s_next_refresh;

/* LED 模式（ADR-008）：返回 true = 亮（PC13 低有效，写引脚时取反） */
static bool led_on(uint32_t now)
{
    uint32_t m;
    switch (s_state) {
    case BL_UI_WAITING:
        return (now % 1000u) < 500u;
    case BL_UI_UPGRADING:
        return (now % 200u) < 100u;
    case BL_UI_APP_INVALID:
        m = now % 2000u;
        return (m < 200u) || (m >= 400u && m < 600u);
    case BL_UI_JUMPING:
        return true;
    case BL_UI_FAULT:
    default:
        m = now % 2000u;
        return (m < 100u) || ((m >= 200u) && (m < 300u)) ||
               ((m >= 400u) && (m < 500u));
    }
}

void bl_ui_init(void)
{
    s_state = BL_UI_WAITING;
    s_progress = 0u;
    s_crc_ok = false;
    s_next_refresh = 0u;
    bl_gpio.write(BL_PIN_LED, true);   /* 高电平 = 灭 */
    ssd1306_clear();
}

void bl_ui_set_state(bl_ui_state_t state) { s_state = state; }

void bl_ui_set_progress(uint8_t percent) { s_progress = percent; }

void bl_ui_set_crc_ok(bool ok) { s_crc_ok = ok; }

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
    case BL_UI_UPGRADING:
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
    ssd1306_puts(8, 5, "WDG:ON ");

    /* 接线诊断行：RX=累计收到字节，VF=已解析送达的有效帧（protocol.md §4.2） */
    {
        char diag[22];
        uint8_t k = 0;
        diag[k++] = 'R'; diag[k++] = 'X'; diag[k++] = ':';
        k += fmt_dec(diag + k, bl_uart_port_rx_total(), 7u);
        diag[k++] = ' '; diag[k++] = 'V'; diag[k++] = 'F'; diag[k++] = ':';
        (void)fmt_dec(diag + k, bl_protocol_stat_delivered(), 7u);
        diag[21] = '\0';
        ssd1306_puts(0, 6, diag);
    }
}

void bl_ui_tick(uint32_t now_ms)
{
    /* LED：模式表驱动，非阻塞 */
    bl_gpio.write(BL_PIN_LED, !led_on(now_ms));

    /* OLED：限频 + 每次调用至多发一个页条带（分片间回主循环，architecture.md §8） */
    if ((int32_t)(now_ms - s_next_refresh) >= 0) {
        s_next_refresh = now_ms + BL_UI_REFRESH_MS;
        refresh_screen();
    }
    (void)ssd1306_flush_strips();
}
