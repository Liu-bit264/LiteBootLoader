#ifndef F1_BITS_H
#define F1_BITS_H
/* DFP 2.4.1 的 stm32f10x.h 缺失的 GPIO 引脚掩码补遗（RM0008 GPIOx_BSRR/BRR 位定义）。
   带守卫：若未来器件头补齐这些宏则自动让位。 */
#include "stm32f10x.h"

#ifndef GPIO_Pin_0
#define GPIO_Pin_0  ((uint16_t)0x0001)
#endif
#ifndef GPIO_Pin_1
#define GPIO_Pin_1  ((uint16_t)0x0002)
#endif
#ifndef GPIO_Pin_8
#define GPIO_Pin_8  ((uint16_t)0x0100)
#endif
#ifndef GPIO_Pin_9
#define GPIO_Pin_9  ((uint16_t)0x0200)
#endif
#ifndef GPIO_Pin_13
#define GPIO_Pin_13 ((uint16_t)0x2000)
#endif

#endif /* F1_BITS_H */
