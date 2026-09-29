#include "gpio.h"
#include "board_config.h"
#include "bl_port.h"
#include "stm32f4xx.h"

/* 板级引脚抽象（ADR-018）：物理引脚唯一出处为 board_config.h，本文件仅消费。
   端口序号 -> 寄存器基址（0=GPIOA 1=GPIOB 2=GPIOC，与声明端约定一致） */
static GPIO_TypeDef *const k_ports[] = { GPIOA, GPIOB, GPIOC };

typedef struct {
    uint8_t  port_idx;   /* board_config *_PORT 端口序号 */
    uint16_t mask;       /* 1u << *_NUM 位掩码 */
} pin_map_t;

static const pin_map_t k_pins[] = {
    [BL_PIN_LED]       = { BL_PIN_LED_PORT,      1u << BL_PIN_LED_NUM },
    [BL_PIN_BT_STATE]  = { BL_PIN_BT_STATE_PORT, 1u << BL_PIN_BT_STATE_NUM },
};
#define PIN_COUNT (sizeof(k_pins) / sizeof(k_pins[0]))
#define PIN_PORT(m) (k_ports[(m).port_idx])
#define PIN_MASK(m) ((m).mask)

void bl_gpio_port_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    /* LED：开漏输出（低电平点亮，灌电流；与 F103 方案一致），按声明端 NUM 派生位段 */
    const pin_map_t *led = &k_pins[BL_PIN_LED];
    PIN_PORT(*led)->MODER = (PIN_PORT(*led)->MODER & ~(3u << (2u * BL_PIN_LED_NUM))) |
                            (1u << (2u * BL_PIN_LED_NUM));   /* 输出 */
    PIN_PORT(*led)->OTYPER |= PIN_MASK(*led);                /* 开漏 */
    PIN_PORT(*led)->BSRR = PIN_MASK(*led);                   /* 高 = 灭 */

    /* BT_STATE：浮空输入（本包 NC——未接 HC-05，读值无意义，
       仅保持 OTA_QUERY 字段语义；接蓝牙的支持包改为输入下拉并接 STATE） */
    const pin_map_t *st = &k_pins[BL_PIN_BT_STATE];
    PIN_PORT(*st)->MODER &= ~(3u << (2u * BL_PIN_BT_STATE_NUM));   /* 输入 */
    PIN_PORT(*st)->PUPDR &= ~(3u << (2u * BL_PIN_BT_STATE_NUM));   /* 浮空 */
}

static void set_pin(const pin_map_t *m, bool level)
{
    if (level) {
        PIN_PORT(*m)->BSRR = PIN_MASK(*m);
    } else {
        PIN_PORT(*m)->BSRR = PIN_MASK(*m) << 16u;   /* F4 无 BRR，BSRR 高 16 位复位 */
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
        set_pin(&k_pins[pin_id], (PIN_PORT(k_pins[pin_id])->ODR & PIN_MASK(k_pins[pin_id])) == 0u);
    }
}

bool bl_gpio_port_read(uint8_t pin_id)
{
    if (pin_id < PIN_COUNT) {
        return (PIN_PORT(k_pins[pin_id])->IDR & PIN_MASK(k_pins[pin_id])) != 0u;
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
