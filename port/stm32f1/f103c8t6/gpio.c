#include "gpio.h"
#include "board_config.h"
#include "bl_port.h"
#include "f1_bits.h"
#include "stm32f10x.h"

/* 引脚映射表：id -> (GPIOx, bit)。新增板级输出在此登记 */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} pin_map_t;

static const pin_map_t k_pins[] = {
    [BL_PIN_LED]       = { GPIOC, GPIO_Pin_13 },
    [BL_PIN_I2C_SCL]   = { GPIOB, GPIO_Pin_8 },
    [BL_PIN_I2C_SDA]   = { GPIOB, GPIO_Pin_9 },
    [BL_PIN_BT_STATE]  = { GPIOB, GPIO_Pin_0 },
    [BL_PIN_BT_EN]     = { GPIOB, GPIO_Pin_1 },
};
#define PIN_COUNT (sizeof(k_pins) / sizeof(k_pins[0]))

void bl_gpio_port_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN;

    /* PC13 LED：通用开漏输出 2MHz（低电平点亮） */
    GPIOC->CRH &= ~(GPIO_CRH_MODE13 | GPIO_CRH_CNF13);
    GPIOC->CRH |= GPIO_CRH_MODE13_1;              /* output 2MHz */
    GPIOC->BSRR = GPIO_Pin_13;                    /* 高 = 灭 */

    /* PB0 BT_STATE：输入下拉（CNF=10/MODE=00/ODR=0）——模块未接或未连接时读 0 */
    GPIOB->CRL &= ~(GPIO_CRL_MODE0 | GPIO_CRL_CNF0);
    GPIOB->CRL |= GPIO_CRL_CNF0_1;
    GPIOB->BRR = GPIO_Pin_0;

    /* PB1 BT_EN：通用推挽输出 2MHz，默认低 = HC-05 上电进数据模式
       （bluetooth_notes.md §2：AT 模式要求 KEY 上电时为高，固件不运行时切换） */
    GPIOB->CRL &= ~(GPIO_CRL_MODE1 | GPIO_CRL_CNF1);
    GPIOB->CRL |= GPIO_CRL_MODE1_1;
    GPIOB->BRR = GPIO_Pin_1;

    /* PB8/PB9 交由 bl_i2c 配置为开漏（i2c.c 自己初始化） */
}

static void set_pin(const pin_map_t *m, bool level)
{
    if (level) {
        m->port->BSRR = m->pin;
    } else {
        m->port->BRR = m->pin;
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
