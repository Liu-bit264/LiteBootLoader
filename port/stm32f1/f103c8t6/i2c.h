#ifndef BL_I2C_H
#define BL_I2C_H
/* 软件 I2C（PB8=SCL / PB9=SDA，开漏，ADR 硬件 I2C1 重映射为编译期备选，本期不启用） */
#include <stdint.h>
#include <stdbool.h>

void bl_i2c_port_init(void);
void bl_i2c_port_release(void);   /* 引脚释放为模拟输入（跳转第 6 步） */
bool bl_i2c_probe(uint8_t dev_addr);   /* 设备 ACK 探测（诊断用） */

#endif /* BL_I2C_H */
