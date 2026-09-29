#include "gpio.h"
#include "board_config.h"
#include "bl_port.h"
#include "stm32f4xx.h"

/* 引脚映射表：id -> (GPIOx, bit)。新增板级输出在此登记（F4 MODER/BSRR 模型） */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} pin_map_t;

static const pin_map_t k_pins[] = {
    [BL_PIN_LED]       = { GPIOC, GPIO_ODR_ODR_13 },
    [BL_PIN_BT_STATE]  = { GPIOC, GPIO_ODR_ODR_14 },
};
#define PIN_COUNT (sizeof(k_pins) / sizeof(k_pins[0]))

void bl_gpio_port_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    /* PC13 LED：开漏输出低速（低电平点亮，灌电流；与 F103 方案一致） */
    GPIOC->MODER &= ~(3u << (13u * 2u));
    GPIOC->MODER |= 1u << (13u * 2u);             /* 输出 */
    GPIOC->OTYPER |= GPIO_OTYPER_OT13;            /* 开漏 */
    GPIOC->BSRR = GPIO_ODR_ODR_13;                /* 高 = 灭 */

    /* PC14 BT_STATE：浮空输入（本包 NC——未接 HC-05，读值无意义，
       仅保持 OTA_QUERY 字段语义；接蓝牙的支持包改为输入下拉并接 STATE） */
    GPIOC->MODER &= ~(3u << (14u * 2u));          /* 输入 */
    GPIOC->PUPDR &= ~(3u << (14u * 2u));          /* 浮空 */
}

static void set_pin(const pin_map_t *m, bool level)
{
    if (level) {
        m->port->BSRR = m->pin;
    } else {
        m->port->BSRR = m->pin << 16u;   /* F4 无 BRR，BSRR 高 16 位复位 */
    }
}

void bl_gpio_port_write(uint8_t pin_id, bool level)
{
    if (pin_id < PIN_COUNT) {
        set_pin(&k_pins[pin_id], level);
    }
}

void bl_gpio_port_toggle(uint8_t pin_id)
{
    if (pin_id < PIN_COUNT) {
        set_pin(&k_pins[pin_id], (k_pins[pin_id].port->ODR & k_pins[pin_id].pin) == 0u);
    }
}

bool bl_gpio_port_read(uint8_t pin_id)
{
    if (pin_id < PIN_COUNT) {
        return (k_pins[pin_id].port->IDR & k_pins[pin_id].pin) != 0u;
    }
    return false;
}

/* ---- bl_gpio_ops ---- */
static void ops_init(void) { bl_gpio_port_init(); }
static void ops_write(uint8_t id, bool l) { bl_gpio_port_write(id, l); }
static void ops_toggle(uint8_t id) { bl_gpio_port_toggle(id); }
static bool ops_read(uint8_t id) { return bl_gpio_port_read(id); }

const bl_gpio_ops bl_gpio = {
    .init = ops_init,
    .write = ops_write,
    .toggle = ops_toggle,
    .read = ops_read,
};

/* F411 最小包无 I2C/OLED：bl_boot 九步第 6 步的释放钩子落为空实现
   （F1 由 i2c.c 提供强符号，此处直接给出定义） */
void bl_port_i2c_release(void)
{
}
