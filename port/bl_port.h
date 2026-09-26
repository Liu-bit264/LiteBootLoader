#ifndef BL_PORT_H
#define BL_PORT_H
/* 端口抽象层（architecture.md §3）：core 只依赖本头文件，禁止包含芯片/CMSIS 头 */
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    void (*init)(void);
    bool (*read)(uint8_t *buf, uint32_t len, uint32_t *out_len);   /* 非阻塞 */
    bool (*write)(const uint8_t *buf, uint32_t len);               /* 阻塞 */
} bl_uart_ops;

typedef struct {
    void (*init)(void);
    bool (*read)(uint32_t addr, uint8_t *buf, uint32_t len);
    bool (*write)(uint32_t addr, const uint8_t *data, uint32_t len);  /* 半字对齐，尾部自动补 0xFF */
    /* 擦除单元（ADR-015，替代旧"页"抽象）：F1 为均匀 1K 页；F4 为非均匀扇区，
       unit_addr/unit_size 按单元序号查表，core 不假设单元等大 */
    uint32_t (*unit_count)(void);                                     /* 全 Flash 擦除单元数 */
    uint32_t (*unit_addr)(uint32_t unit_index);                       /* 单元起始地址（越界返回 0） */
    uint32_t (*unit_size)(uint32_t unit_index);                       /* 单元大小（越界返回 0） */
    bool (*erase_unit)(uint32_t unit_index);                          /* 擦除一个擦除单元 */
    bool (*is_range_valid)(uint32_t addr, uint32_t len);              /* 物理边界防御（board_config 定界） */
} bl_flash_ops;

typedef struct {
    void (*init)(void);
    bool (*write_mem)(uint8_t dev_addr, uint8_t reg, const uint8_t *data, uint16_t len);
    bool (*read_mem)(uint8_t dev_addr, uint8_t reg, uint8_t *buf, uint16_t len);
} bl_i2c_ops;

typedef struct {
    void (*init)(void);
    void (*write)(uint8_t pin_id, bool level);   /* level=true 为高电平 */
    void (*toggle)(uint8_t pin_id);
    bool (*read)(uint8_t pin_id);
} bl_gpio_ops;

typedef struct {
    void (*init)(uint32_t timeout_ms);
    bool (*set_timeout_ms)(uint32_t timeout_ms);  /* 运行时重配（ADR-015 升级期放宽）；false=不支持或超硬件上限 */
    void (*refresh)(void);
} bl_wdg_ops;

typedef struct {
    void (*init)(void);                  /* HSE->PLL 72MHz，失败回退 HSI（ADR-010） */
    uint32_t (*sysclk_hz)(void);
    uint32_t (*tick_ms)(void);
    void (*delay_ms)(uint32_t ms);
} bl_clock_ops;

extern const bl_uart_ops  bl_uart;
extern const bl_flash_ops bl_flash;
extern const bl_i2c_ops   bl_i2c;
extern const bl_gpio_ops  bl_gpio;
extern const bl_wdg_ops   bl_wdg;
extern const bl_clock_ops bl_clock;

/* ---- 跳转序列与系统级辅助（architecture.md §3，bl_boot 九步使用） ---- */
void bl_port_disable_irq(void);
void bl_port_stop_systick(void);
void bl_port_uart_deinit(void);
void bl_port_i2c_release(void);            /* PB8/PB9 释放为模拟输入 */
void bl_port_clear_pending_irqs(void);     /* NVIC ICPR 全清 */
void bl_port_set_vtor(uint32_t addr);
void bl_port_set_msp(uint32_t value);
/* 注意：set_msp/jump 两个 C 辅助不得用于真实跳转路径——set_msp 切换 MSP 后
   自身的函数尾声会从新栈弹 PC（阶段 2 fault 现场实锤）。真实跳转用下方
   bl_port_switch_msp_and_jump（真汇编原子序列，bl_jump.s）。 */
void bl_port_jump(uint32_t reset_handler);
void bl_port_switch_msp_and_jump(uint32_t msp, uint32_t entry);  /* 不返回 */
void bl_port_system_reset(void);
uint32_t bl_port_read_word(uint32_t addr);

#endif /* BL_PORT_H */
