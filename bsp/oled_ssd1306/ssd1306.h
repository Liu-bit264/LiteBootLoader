#ifndef SSD1306_H
#define SSD1306_H
/* SSD1306 0.96" OLED（128x64，I2C 地址 0x3C，页寻址模式） */
#include <stdint.h>
#include <stdbool.h>

void ssd1306_init(void);
void ssd1306_clear(void);
void ssd1306_puts(uint8_t x, uint8_t page, const char *s);   /* 6x8 字体，x 0..122, page 0..7 */
bool ssd1306_flush_strips(void);   /* 每次调用发送一个脏页；返回 true = 仍有未发送页 */

#endif /* SSD1306_H */
