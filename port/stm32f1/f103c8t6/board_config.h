#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H
/* 全项目常量唯一出处（design.md §5 集中配置表）；修改需同步 design.md 变更记录 */

/* ---- Flash 分区（partition.md §1） ---- */
#define BL_FLASH_BASE          0x08000000u
#define BL_FLASH_SIZE          0x00010000u   /* 64 KiB */
#define BL_PAGE_SIZE           0x00000400u   /* 1 KiB */
#define BL_APP_BASE            0x08004000u   /* 页 16 */
#define BL_APP_SIZE            0x0000B800u   /* 46 KiB，页 16-61 */
#define BL_PARAM_BASE          0x0800F800u   /* 页 62 = 副本 A */
#define BL_PARAM_SIZE          0x00000800u   /* 2 KiB，页 62-63 */

/* ---- 擦除单元（ADR-015：升级自"页"抽象；F1 均匀 1K 页，F4 为非均匀扇区表） ----
   core 只经 bl_flash_ops 的 unit_* 接口访问单元几何，不假设单元等大。 */
#define BL_ERASE_UNITS_UNIFORM  1u    /* 1=均匀单元 BASE+SIZE*i；0=显式表（F4 移植时在此定义扇区表） */
#define BL_ERASE_UNIT_BASE      BL_FLASH_BASE
#define BL_ERASE_UNIT_SIZE      BL_PAGE_SIZE
#define BL_ERASE_UNIT_COUNT     (BL_FLASH_SIZE / BL_PAGE_SIZE)   /* 64 */
#define BL_APP_UNITS_MAX        (BL_APP_SIZE / BL_PAGE_SIZE)     /* APP 覆盖单元数上界（位图容量） */
#define BL_PARAM_COPY_SIZE      0x00000400u   /* 参数副本间隔 = 1 页（F4 为一个 16K 扇区） */

/* ---- SRAM（跳转校验范围） ---- */
#define BL_SRAM_BASE           0x20000000u
#define BL_SRAM_SIZE           0x00005000u   /* 20 KiB */

/* ---- 行为常量 ---- */
#define BL_TRANSPORT_BT_EN     1       /* 1=注册蓝牙通道 1（本板接 HC-05，uart2.c 提供 bl_uart_bt）；
                                          0=通道 1 槽位保留（不支持包未接蓝牙，如 f411ceu6 最小包） */
#define BL_USE_HSE             1       /* 1=HSE 8M->PLL 72M（默认：8MHz 晶振已由 ST 标准工程在本板实测 72M 可用）；
                                          0=HSI 8MHz 直驱（晶振异常时的调试回退，UART 115200 仍可用） */
#define BL_BOOT_WAIT_MS        3000u   /* 启动等待窗口（ADR-004） */
#define BL_IWDG_TIMEOUT_MS     2000u   /* ADR-011 */
#define BL_IWDG_UPGRADE_TIMEOUT_MS 2000u  /* 升级擦写期放宽（ADR-015）：ERASE 前生效，VERIFY 完成或跳转前恢复。
                                              F1 取同值（页擦 ~4ms 无风险）；F4 建议 8000——128K 扇区擦除
                                              ~875ms 且单 bank 擦除期间 CPU 停顿无法喂狗 */
#define BL_UART_BAUD           115200u
#define BL_BT_UART_BAUD        115200u /* UART2/HC-05 数据模式（bluetooth_notes.md §5：
                                           模块经 AT+UART 一次性配置，勿在固件做运行时 AT） */
#define BL_PROTOCOL_ACTIVE_MS  10000u  /* 协议活跃判定（ADR-008/009） */
#define BL_UI_REFRESH_MS       200u    /* OLED 最低刷新间隔 */
#define BL_DISPLAY_USER_PAGE   0       /* 1=等待/升级模式下调用 bl_display_user_page()
                                          弱钩子由用户绘制自检页（AGENTS §8.1）；0=标准页 */
#define BL_RX_RING_SIZE        512u    /* architecture.md §6 */
#define BL_FRAME_DATA_MAX      256u    /* protocol.md §4 */
#define BL_FRAME_BYTE_TIMEOUT_MS 2000u /* 帧内字节间超时（protocol.md §4.2）。2026-09-26 定版：
                                          假触发根因是 poll 用循环顶旧时间戳做无符号减法、毫秒边界
                                          跨越时回绕（selftest 遥测实锤），已改现场重读时刻；阈值
                                          取 2s 容忍 USB/CDC 转发抖动，主机重试间隔需 ≥2s */
#define BL_VERIFY_CHUNK        1024u   /* VERIFY 分块（喂狗粒度） */
#define BL_LOG_HEARTBEAT_MS    500u    /* 空闲心跳日志间隔（0=关闭；协议活跃期静默；接线探针模式） */

/* ---- 板级引脚（ADR-018：本段为引脚事实唯一出处，gpio.c/i2c.c 仅消费） ----
   逻辑 id（BL_PIN_*）供 core/services 引用；物理声明 *_PORT 为端口序号
   （0=GPIOA 1=GPIOB 2=GPIOC…），*_NUM 为引脚号。chips/f103c8t6.json pins 段与
   本段一致性由 chips/test_chip.py 强制。 */
#define BL_UART_TX_PORT        0u      /* 升级串口 TX（实验：UART 引脚同入板级声明） */
#define BL_UART_TX_NUM         9u      /* PA9 */
#define BL_UART_RX_PORT        0u
#define BL_UART_RX_NUM         10u     /* PA10 */
#define BL_PIN_LED             0u      /* 逻辑 id：LED，低电平点亮（ADR-008） */
#define BL_PIN_LED_PORT        2u      /* PC13 */
#define BL_PIN_LED_NUM         13u
#define BL_PIN_LED_ACTIVE_LOW  1
#define BL_PIN_I2C_SCL         1u      /* 逻辑 id：OLED 软件 I2C 时线（开漏配置在 i2c.c） */
#define BL_PIN_I2C_SCL_PORT    1u      /* PB8 */
#define BL_PIN_I2C_SCL_NUM     8u
#define BL_PIN_I2C_SDA         2u      /* 逻辑 id：OLED 软件 I2C 数据线 */
#define BL_PIN_I2C_SDA_PORT    1u      /* PB9 */
#define BL_PIN_I2C_SDA_NUM     9u
#define BL_PIN_BT_STATE        3u      /* 逻辑 id：HC-05 STATE 输入，高=SPP 已连接（输入下拉） */
#define BL_PIN_BT_STATE_PORT   1u      /* PB0 */
#define BL_PIN_BT_STATE_NUM    0u
#define BL_PIN_BT_EN           4u      /* 逻辑 id：HC-05 EN 输出，默认低=数据模式
                                           （bluetooth_notes.md §2，运行时翻转进 AT 不可靠，仅预留） */
#define BL_PIN_BT_EN_PORT      1u      /* PB1 */
#define BL_PIN_BT_EN_NUM       1u

/* ---- F103 系统内存区（GET_INFO 用） ---- */
#define BL_UID_ADDR            0x1FFFF7E8u
#define BL_FLSIZE_ADDR         0x1FFFF7E0u

#endif /* BOARD_CONFIG_H */
