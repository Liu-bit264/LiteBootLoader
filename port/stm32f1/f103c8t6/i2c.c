#include "i2c.h"
#include "board_config.h"
#include "bl_port.h"
#include "f1_bits.h"
#include "stm32f10x.h"

/* SCL 推挽（单主机，SSD1306 不拉伸时钟，无需外部上拉）；
   SDA 平时推挽，ACK/读取阶段切输入上拉采样——无外部上拉也能工作 */
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
    /* ~5us @72MHz：约 360 NOP，折半周期 -> ~100kHz */
    for (volatile uint32_t i = 0; i < 180u; i++) {
        __NOP();
    }
}

static bool sda_read(void)
{
    sda_mode_in();
    i2c_delay();
    bool level = SDA_READ();
    sda_mode_out();
    return level;
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

static bool i2c_write_byte(uint8_t b)
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
    return sda_read(); /* false = ACK（从机拉低） */
}

static uint8_t i2c_read_byte(bool nack)
{
    sda_mode_in();   /* 读位期间 SDA 必须为输入，避免推挽顶死从机 */
    uint8_t b = 0;
    for (uint8_t i = 0; i < 8u; i++) {
        SCL_H();
        i2c_delay();
        b = (uint8_t)((b << 1) | (SDA_READ() ? 1u : 0u));
        SCL_L();
        i2c_delay();
    }
    /* 主机 ACK/NACK：输入模式下 ODR=0 经下拉给出低电平 */
    if (nack) {
        SDA_H();
    } else {
        SDA_L();
    }
    i2c_delay();
    SCL_H();
    i2c_delay();
    SCL_L();
    sda_mode_out();
    SDA_H();
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
    /* 单字节 NOP 命令探测设备应答（0xE3 = SSD1306 NOP） */
    uint8_t nop = 0xE3u;
    return bl_i2c.write_mem(dev_addr, 0x00u, &nop, 1u);
}

/* ---- bl_i2c_ops ---- */
static void ops_init(void) { bl_i2c_port_init(); }

static bool ops_write_mem(uint8_t dev, uint8_t reg, const uint8_t *data, uint16_t len)
{
    i2c_start();
    if (i2c_write_byte((uint8_t)(dev << 1))) { i2c_stop(); return false; }
    if (i2c_write_byte(reg))               { i2c_stop(); return false; }
    for (uint16_t i = 0; i < len; i++) {
        if (i2c_write_byte(data[i]))       { i2c_stop(); return false; }
    }
    i2c_stop();
    return true;
}

static bool ops_read_mem(uint8_t dev, uint8_t reg, uint8_t *buf, uint16_t len)
{
    i2c_start();
    if (i2c_write_byte((uint8_t)(dev << 1))) { i2c_stop(); return false; }
    if (i2c_write_byte(reg))               { i2c_stop(); return false; }
    i2c_start();
    if (i2c_write_byte((uint8_t)((dev << 1) | 1u))) { i2c_stop(); return false; }
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = i2c_read_byte(i == (len - 1u));
    }
    i2c_stop();
    return true;
}

const bl_i2c_ops bl_i2c = {
    .init = ops_init,
    .write_mem = ops_write_mem,
    .read_mem = ops_read_mem,
};
