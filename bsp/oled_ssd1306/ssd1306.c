#include "ssd1306.h"
#include "ssd1306_font.h"
#include "i2c.h"
#include "bl_port.h"
#include "board_config.h"

#define SSD1306_ADDR   0x3Cu
#define FB_PAGES       8u
#define FB_COLS        128u

static uint8_t s_fb[FB_PAGES][FB_COLS];
static uint8_t s_dirty;        /* 页脏位图 */
static uint8_t s_flush_next;   /* 下一个待发送页 */

static void cmd(uint8_t c) { (void)bl_i2c.write_mem(SSD1306_ADDR, 0x00u, &c, 1u); }

static void cmd2(uint8_t a, uint8_t b)
{
    uint8_t buf[2] = { a, b };
    (void)bl_i2c.write_mem(SSD1306_ADDR, 0x00u, buf, 2u);
}

void ssd1306_init(void)
{
    bl_i2c.init();
    /* 上电稳定等待：SSD1306 需 VDD 稳定后 ~100ms 才接受命令（经典全黑根因） */
    bl_wdg.refresh();
    bl_clock.delay_ms(120u);
    /* ACK 探测重试：覆盖模块间上电差异；无应答则放弃初始化（不阻塞系统） */
    bool present = false;
    for (uint32_t t = 0; t < 40u; t++) {
        if (bl_i2c_probe(SSD1306_ADDR)) {
            present = true;
            break;
        }
        bl_wdg.refresh();
        bl_clock.delay_ms(5u);
    }
    if (!present) {
        return;
    }
    /* 标准初始化序列（数据手册） */
    cmd(0xAEu);                    /* display off */
    cmd2(0xD5u, 0x80u);            /* clock divide */
    cmd2(0xA8u, 0x3Fu);            /* mux 1/64 */
    cmd2(0xD3u, 0x00u);            /* display offset */
    cmd(0x40u);                    /* start line 0 */
    cmd2(0x8Du, 0x14u);            /* charge pump on */
    cmd2(0x20u, 0x02u);            /* 页寻址模式 */
    cmd(0xA1u);                    /* segment remap */
    cmd(0xC8u);                    /* COM scan direction */
    cmd2(0xDAu, 0x12u);            /* COM pins */
    cmd2(0x81u, 0xCFu);            /* contrast */
    cmd2(0xD9u, 0xF1u);            /* precharge */
    cmd2(0xDBu, 0x40u);            /* VCOMH */
    cmd(0xA4u);                    /* resume from RAM */
    cmd(0xA6u);                    /* normal display */
    cmd(0xAFu);                    /* display on */
}

void ssd1306_clear(void)
{
    for (uint32_t p = 0; p < FB_PAGES; p++) {
        for (uint32_t c = 0; c < FB_COLS; c++) {
            s_fb[p][c] = 0u;
        }
    }
    s_dirty = 0xFFu;
}

void ssd1306_puts(uint8_t x, uint8_t page, const char *s)
{
    if (page >= FB_PAGES) {
        return;
    }
    while (*s && x < (FB_COLS - FONT_COLS)) {
        uint8_t ch = (uint8_t)*s++;
        if (ch < FONT_FIRST_CHAR || ch > FONT_LAST_CHAR) {
            ch = (uint8_t)'?';
        }
        const uint8_t *glyph = k_font5x7[ch - FONT_FIRST_CHAR];
        for (uint32_t i = 0; i < FONT_COLS; i++) {
            s_fb[page][x] = glyph[i];
            x++;
        }
        s_fb[page][x] = 0x00u;   /* 字间距 */
        x++;
    }
    s_dirty |= (uint8_t)(1u << page);
}

bool ssd1306_flush_strips(void)
{
    /* 每次调用至多发一个页（非阻塞分片，architecture.md §8） */
    if (s_dirty == 0u) {
        return false;
    }
    /* 从上次位置起找下一个脏页 */
    for (uint32_t k = 0; k < FB_PAGES; k++) {
        uint8_t page = (uint8_t)((s_flush_next + k) % FB_PAGES);
        if (s_dirty & (1u << page)) {
            cmd((uint8_t)(0xB0u | page));        /* 页地址 */
            cmd(0x00u);                          /* 列低 4 位 */
            cmd(0x10u);                          /* 列高 4 位 */
            (void)bl_i2c.write_mem(SSD1306_ADDR, 0x40u, s_fb[page], FB_COLS);
            bl_wdg.refresh();   /* 喂狗点：OLED 条带之间 */
            s_dirty &= (uint8_t)~(1u << page);
            s_flush_next = (uint8_t)((page + 1u) % FB_PAGES);
            return s_dirty != 0u;
        }
    }
    s_dirty = 0u;
    return false;
}
