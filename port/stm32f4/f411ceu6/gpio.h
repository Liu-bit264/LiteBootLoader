#ifndef BL_GPIO_H
#define BL_GPIO_H
/* PC13 LED（低电平点亮）等板级输出的端口映射（F4 MODER/BSRR 模型） */
#include <stdint.h>
#include <stdbool.h>

void bl_gpio_port_init(void);
void bl_gpio_port_write(uint8_t pin_id, bool level);
void bl_gpio_port_toggle(uint8_t pin_id);
bool bl_gpio_port_read(uint8_t pin_id);

#endif /* BL_GPIO_H */
