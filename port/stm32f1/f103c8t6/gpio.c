#include "gpio.h"
#include "board_config.h"
#include "bl_port.h"
#include "stm32f10x.h"

/* 板级引脚抽象（ADR-018）：物理引脚唯一出处为 board_config.h，本文件仅消费。
   端口序号 -> 寄存器基址（0=GPIOA 1=GPIOB 2=GPIOC，与声明端约定一致） */
static GPIO_TypeDef *const k_ports[] = { GPIOA, GPIOB, GPIOC };

typedef struct {
    uint8_t  port_idx;   /* board_config *_PORT 端口序号 */
    uint16_t mask;       /* 1u << *_NUM 位掩码 */
} pin_map_t;

static const pin_map_t k_pins[] = {
    [BL_PIN_LED]       = { BL_PIN_LED_PORT,      1u << BL_PIN_LED_NUM },
    [BL_PIN_I2C_SCL]   = { BL_PIN_I2C_SCL_PORT,  1u << BL_PIN_I2C_SCL_NUM },
    [BL_PIN_I2C_SDA]   = { BL_PIN_I2C_SDA_PORT,  1u << BL_PIN_I2C_SDA_NUM },
    [BL_PIN_BT_STATE]  = { BL_PIN_BT_STATE_PORT, 1u << BL_PIN_BT_STATE_NUM },
    [BL_PIN_BT_EN]     = { BL_PIN_BT_EN_PORT,    1u << BL_PIN_BT_EN_NUM },
};
#define PIN_COUNT (sizeof(k_pins) / sizeof(k_pins[0]))
#define PIN_PORT(m) (k_ports[(m).port_idx])
#define PIN_MASK(m) ((m).mask)

/* F1 CR 配置（每脚 4bit：MODE[1:0]+CNF[3:2]；CRL=pin0-7，CRH=pin8-15） */
#define CR_OUT_PP_2MHZ  0x2u   /* MODE=10 CNF=00 */
#define CR_OUT_OD_2MHZ  0x6u   /* MODE=10 CNF=01 */
#define CR_IN_PUPD      0x8u   /* MODE=00 CNF=10（上/下拉由 ODR 选择） */

static void cr_cfg(GPIO_TypeDef *port, uint8_t num, uint8_t nibble)
{
    volatile uint32_t *cr = (num < 8u) ? &port->CRL : &port->CRH;
    const uint32_t shift = (uint32_t)(num % 8u) * 4u;
    *cr &= ~(0xFu << shift);
    *cr |= (uint32_t)nibble << shift;
}

void bl_gpio_port_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN;

    /* F1 CR 配置（CRL=pin0-7/CRH=pin8-15，每脚 4bit：MODE[1:0]+CNF[3:2]），
       按声明端 NUM 派生 */
    const pin_map_t *led = &k_pins[BL_PIN_LED];
    cr_cfg(PIN_PORT(*led), BL_PIN_LED_NUM, CR_OUT_OD_2MHZ);
    PIN_PORT(*led)->BSRR = PIN_MASK(*led);              /* 高 = 灭 */

    /* BT_STATE：输入下拉（MODE=00/CNF=10，ODR=0 选下拉）——模块未接或未连接时读 0 */
    const pin_map_t *st = &k_pins[BL_PIN_BT_STATE];
    cr_cfg(PIN_PORT(*st), BL_PIN_BT_STATE_NUM, CR_IN_PUPD);
    PIN_PORT(*st)->BRR = PIN_MASK(*st);

    /* BT_EN：输出推挽，默认低 = HC-05 上电进数据模式
       （bluetooth_notes.md §2：AT 模式要求 KEY 上电时为高，固件不运行时切换） */
    const pin_map_t *en = &k_pins[BL_PIN_BT_EN];
    cr_cfg(PIN_PORT(*en), BL_PIN_BT_EN_NUM, CR_OUT_PP_2MHZ);
    PIN_PORT(*en)->BRR = PIN_MASK(*en);

    /* I2C 两线交由 bl_i2c 配置为开漏（i2c.c 自己初始化，声明见 board_config） */
}

static void set_pin(const pin_map_t *m, bool level)
{
    if (level) {
        PIN_PORT(*m)->BSRR = PIN_MASK(*m);
    } else {
        PIN_PORT(*m)->BRR = PIN_MASK(*m);
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
