#ifndef SSD1306_FONT_H
#define SSD1306_FONT_H
#include <stdint.h>

#define FONT_FIRST_CHAR 0x20u
#define FONT_LAST_CHAR  0x7Eu
#define FONT_COLS       5u

extern const uint8_t k_font5x7[95][5];

#endif /* SSD1306_FONT_H */
