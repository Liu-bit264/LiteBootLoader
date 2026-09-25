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
    bool (*erase_page)(uint32_t page_index);
    bool (*erase_range)(uint32_t addr, uint32_t len);                 /* 页对齐区间 */
    uint32_t (*page_size)(void);
    bool (*is_range_valid)(uint32_t addr, uint32_t len);              /* 物理边界防御（64 KiB） */
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
void bl_port_jump(uint32_t reset_handler);
void bl_port_system_reset(void);
uint32_t bl_port_read_word(uint32_t addr);

#endif /* BL_PORT_H */
