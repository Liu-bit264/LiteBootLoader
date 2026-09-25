#include "i2c.h"
#include "board_config.h"
#include "bl_port.h"
#include "f1_bits.h"
#include "stm32f10x.h"

/* 软件 I2C（PB8=SCL / PB9=SDA）：
   - SCL 推挽（单主机，SSD1306 不拉伸时钟，无需外部上拉）
   - SDA 平时推挽；ACK/读取阶段切输入上拉采样
   - ACK 在第 9 个 SCL 高电平期间采样（协议正确时序，勿改回 SCL 低电平采样）
   - 命令传输尽力而为：NACK 不中止事务（SSD1306 write-only，漏时钟才是致命的） */

#define SCL_H() (GPIOB->BSRR = GPIO_Pin_8)
#define SCL_L() (GPIOB->BRR = GPIO_Pin_8)
#define SDA_H() (GPIOB->BSRR = GPIO_Pin_9)
#define SDA_L() (GPIOB->BRR = GPIO_Pin_9)
#define SDA_READ() ((GPIOB->IDR & GPIO_Pin_9) != 0u)

static void sda_mode_out(void)
{
    /* 推挽输出 2MHz：CNF9=00, MODE9=10 */
    GPIOB->CRH = (GPIOB->CRH & ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9)) | GPIO_CRH_MODE9_1;
}

static void sda_mode_in(void)
{
    /* 输入上拉：MODE9=00, CNF9=10, ODR9=1（F1 输入模式下 ODR 选上/下拉） */
    GPIOB->CRH = (GPIOB->CRH & ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9)) | GPIO_CRH_CNF9_1;
    GPIOB->BSRR = GPIO_Pin_9;
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
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    /* PB8：推挽输出 2MHz（SCL）；PB9：推挽输出（SDA，ACK 期动态切输入） */
    GPIOB->CRH &= ~(GPIO_CRH_MODE8 | GPIO_CRH_CNF8);
    GPIOB->CRH |= GPIO_CRH_MODE8_1;               /* CNF8=00 推挽 */
    sda_mode_out();
    SCL_H();
    SDA_H();
}

void bl_i2c_port_release(void)
{
    GPIOB->CRH &= ~(GPIO_CRH_MODE8 | GPIO_CRH_CNF8 | GPIO_CRH_MODE9 | GPIO_CRH_CNF9);
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
