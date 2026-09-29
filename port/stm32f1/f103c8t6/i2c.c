#include "i2c.h"
#include "board_config.h"
#include "bl_port.h"
#include "f1_bits.h"
#include "stm32f10x.h"

/* 软件 I2C（引脚消费 board_config 的 BL_PIN_I2C_SCL/SDA，默认 PB8=SCL / PB9=SDA）：
   - SCL 推挽（单主机，SSD1306 不拉伸时钟，无需外部上拉）
   - SDA 平时推挽；ACK/读取阶段切输入上拉采样
   - ACK 在第 9 个 SCL 高电平期间采样（协议正确时序，勿改回 SCL 低电平采样）
   - 命令传输尽力而为：NACK 不中止事务（SSD1306 write-only，漏时钟才是致命的）

   宏化（ADR-018；审计 2026-09-29 补充项）：端口由 GPIOA_BASE + 0x400*序号派生
   （F1 GPIO 端口间隔 0x400），CRL/CRH 与位段在编译期由 *_NUM 选择——CSP 引脚
   是编译期常量，零运行时开销。 */
#define I2C_GPIO(port)  ((GPIO_TypeDef *)(GPIOA_BASE + 0x400u * (port)))
#define I2C_SCL_GPIO    I2C_GPIO(BL_PIN_I2C_SCL_PORT)
#define I2C_SDA_GPIO    I2C_GPIO(BL_PIN_I2C_SDA_PORT)
#define I2C_SCL_MASK    (1u << BL_PIN_I2C_SCL_NUM)
#define I2C_SDA_MASK    (1u << BL_PIN_I2C_SDA_NUM)

/* 引脚 0-7 配置位在 CRL、8-15 在 CRH（编译期选择），每引脚 4 位 CNF[1:0]+MODE[1:0] */
#if BL_PIN_I2C_SCL_NUM < 8u
#define I2C_SCL_CR      I2C_SCL_GPIO->CRL
#define I2C_SCL_SHIFT   (4u * BL_PIN_I2C_SCL_NUM)
#else
#define I2C_SCL_CR      I2C_SCL_GPIO->CRH
#define I2C_SCL_SHIFT   (4u * (BL_PIN_I2C_SCL_NUM - 8u))
#endif
#if BL_PIN_I2C_SDA_NUM < 8u
#define I2C_SDA_CR      I2C_SDA_GPIO->CRL
#define I2C_SDA_SHIFT   (4u * BL_PIN_I2C_SDA_NUM)
#else
#define I2C_SDA_CR      I2C_SDA_GPIO->CRH
#define I2C_SDA_SHIFT   (4u * (BL_PIN_I2C_SDA_NUM - 8u))
#endif
#define I2C_MODE_PP2M(sh)   (0x2u << (sh))   /* MODE=10（2MHz）CNF=00（推挽） */
#define I2C_MODE_IN_PU(sh)  (0x8u << (sh))   /* MODE=00 CNF=10（输入上拉） */

#define SCL_H() (I2C_SCL_GPIO->BSRR = I2C_SCL_MASK)
#define SCL_L() (I2C_SCL_GPIO->BRR = I2C_SCL_MASK)
#define SDA_H() (I2C_SDA_GPIO->BSRR = I2C_SDA_MASK)
#define SDA_L() (I2C_SDA_GPIO->BRR = I2C_SDA_MASK)
#define SDA_READ() ((I2C_SDA_GPIO->IDR & I2C_SDA_MASK) != 0u)

static void sda_mode_out(void)
{
    /* 推挽输出 2MHz（CRx 位段由编译期选择） */
    I2C_SDA_CR = (I2C_SDA_CR & ~(0xFu << I2C_SDA_SHIFT)) | I2C_MODE_PP2M(I2C_SDA_SHIFT);
}

static void sda_mode_in(void)
{
    /* 输入上拉：MODE=00, CNF=10, ODR=1（F1 输入模式下 ODR 选上/下拉） */
    I2C_SDA_CR = (I2C_SDA_CR & ~(0xFu << I2C_SDA_SHIFT)) | I2C_MODE_IN_PU(I2C_SDA_SHIFT);
    SDA_H();
}

static void i2c_delay(void)
{
    /* ~5us @72MHz：半周期 -> ~100kHz */
    for (volatile uint32_t i = 0; i < 180u; i++) {
        __NOP();
    }
}

static void i2c_start(void)
{
    SDA_H();
    SCL_H();
    i2c_delay();
    SDA_L();
    i2c_delay();
    SCL_L();
    i2c_delay();
}

static void i2c_stop(void)
{
    SDA_L();
    i2c_delay();
    SCL_H();
    i2c_delay();
    SDA_H();
    i2c_delay();
}

/* 发送 8 位（结束时 SCL 为低）；ACK 由调用方 i2c_get_ack 采样 */
static void i2c_write_byte(uint8_t b)
{
    for (uint8_t i = 0; i < 8u; i++) {
        if (b & 0x80u) {
            SDA_H();
        } else {
            SDA_L();
        }
        b <<= 1;
        i2c_delay();
        SCL_H();
        i2c_delay();
        SCL_L();
    }
}

/* 第 9 个时钟采样从机 ACK：true=ACK（SDA 低），false=NACK */
static bool i2c_get_ack(void)
{
    sda_mode_in();
    i2c_delay();
    SCL_H();
    i2c_delay();
    bool nack = SDA_READ();
    SCL_L();
    sda_mode_out();
    SDA_H();
    i2c_delay();
    return !nack;
}

/* 读取 8 位（调用前 SDA 须处于输入态）；结束时 SCL 为低 */
static uint8_t i2c_read_byte(bool nack)
{
    uint8_t b = 0;
    for (uint8_t i = 0; i < 8u; i++) {
        SCL_H();
        i2c_delay();
        b = (uint8_t)((b << 1) | (SDA_READ() ? 1u : 0u));
        SCL_L();
        i2c_delay();
    }
    /* 主机 ACK/NACK：输入模式下经 ODR 上/下拉驱动第 9 个时钟 */
    if (nack) {
        SDA_H();
    } else {
        SDA_L();
    }
    i2c_delay();
    SCL_H();
    i2c_delay();
    SCL_L();
    i2c_delay();
    return b;
}

void bl_i2c_port_init(void)
{
    /* 端口时钟使能按板级端口序号派生（APB2ENR 的 IOPxEN 各端口连续） */
    RCC->APB2ENR |= (RCC_APB2ENR_IOPAEN << BL_PIN_I2C_SCL_PORT) |
                    (RCC_APB2ENR_IOPAEN << BL_PIN_I2C_SDA_PORT);
    /* SCL：推挽输出 2MHz；SDA：推挽输出（ACK 期动态切输入） */
    I2C_SCL_CR = (I2C_SCL_CR & ~(0xFu << I2C_SCL_SHIFT)) | I2C_MODE_PP2M(I2C_SCL_SHIFT);
    sda_mode_out();
    SCL_H();
    SDA_H();
}

void bl_i2c_port_release(void)
{
    I2C_SCL_CR &= ~(0xFu << I2C_SCL_SHIFT);
    I2C_SDA_CR &= ~(0xFu << I2C_SDA_SHIFT);
}

void bl_port_i2c_release(void) { bl_i2c_port_release(); }

bool bl_i2c_probe(uint8_t dev_addr)
{
    /* 只发地址字节并采样 ACK（诊断用） */
    sda_mode_out();
    i2c_start();
    i2c_write_byte((uint8_t)(dev_addr << 1));
    bool ack = i2c_get_ack();
    i2c_stop();
    return ack;
}

/* ---- bl_i2c_ops ---- */
static void ops_init(void) { bl_i2c_port_init(); }

static bool ops_write_mem(uint8_t dev, uint8_t reg, const uint8_t *data, uint16_t len)
{
    /* 尽力而为：逐字节采样 ACK 但不中止事务，返回整体 ACK 结果 */
    bool all_acked = true;
    sda_mode_out();
    i2c_start();
    i2c_write_byte((uint8_t)(dev << 1));
    all_acked = i2c_get_ack() && all_acked;
    i2c_write_byte(reg);
    all_acked = i2c_get_ack() && all_acked;
    for (uint16_t i = 0; i < len; i++) {
        i2c_write_byte(data[i]);
        all_acked = i2c_get_ack() && all_acked;
    }
    i2c_stop();
    return all_acked;
}

static bool ops_read_mem(uint8_t dev, uint8_t reg, uint8_t *buf, uint16_t len)
{
    sda_mode_out();
    i2c_start();
    i2c_write_byte((uint8_t)(dev << 1));
    bool all_acked = i2c_get_ack();
    i2c_write_byte(reg);
    all_acked = i2c_get_ack() && all_acked;
    i2c_start();
    i2c_write_byte((uint8_t)((dev << 1) | 1u));
    all_acked = i2c_get_ack() && all_acked;
    sda_mode_in();   /* 读阶段 SDA 保持输入，避免推挽与从机顶死 */
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = i2c_read_byte(i == (len - 1u));
    }
    sda_mode_out();
    SDA_H();
    i2c_stop();
    return all_acked;
}

const bl_i2c_ops bl_i2c = {
    .init = ops_init,
    .write_mem = ops_write_mem,
    .read_mem = ops_read_mem,
};
