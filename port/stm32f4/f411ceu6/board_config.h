#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H
/* 全项目常量唯一出处（design.md §5 集中配置表）；修改需同步 design.md 变更记录。
 * F411CEU6 最小支持包（ADR-017）：仅串口升级 + 引导跳转，无 OLED/蓝牙/I2C。 */

/* ---- Flash 分区（partition.md §1；512K = 4x16K + 64K + 3x128K，RM0383 §3.3） ---- */
#define BL_FLASH_BASE          0x08000000u
#define BL_FLASH_SIZE          0x00080000u   /* 512 KiB */
#define BL_APP_BASE            0x08010000u   /* 扇区 4（64K）起 */
#define BL_APP_SIZE            0x00070000u   /* 448 KiB，扇区 4-7 */
#define BL_PARAM_BASE          0x08008000u   /* 扇区 2 = 副本 A */
#define BL_PARAM_SIZE          0x00008000u   /* 32 KiB，扇区 2-3（双副本各 16K） */

/* ---- 擦除单元（ADR-015：F4 非均匀扇区，显式表；core 经 bl_flash_ops.unit_* 访问） ----
   扇区 0-3 各 16K、扇区 4 64K、扇区 5-7 各 128K（RM0383 §3.3，512K 器件共 8 扇区）。
   典型擦除时间（DS10697 数量级）：16K≈230ms、64K≈490ms、128K≈875ms（最大可翻倍）。 */
#define BL_ERASE_UNITS_UNIFORM  0u
#define BL_ERASE_UNIT_COUNT     8u
#define BL_ERASE_UNIT_TABLE { \
    { 0x08000000u, 0x4000u  }, { 0x08004000u, 0x4000u  }, \
    { 0x08008000u, 0x4000u  }, { 0x0800C000u, 0x4000u  }, \
    { 0x08010000u, 0x10000u }, \
    { 0x08020000u, 0x20000u }, { 0x08040000u, 0x20000u }, { 0x08060000u, 0x20000u } }
#define BL_APP_UNITS_MAX        8u     /* APP 覆盖单元数上界（位图容量），实际 4 扇区 */
#define BL_PARAM_COPY_SIZE      0x00004000u   /* 副本间隔 = 1 个 16K 扇区（独立擦除单元） */

/* ---- SRAM（跳转校验范围；128K 连续，无 CCM） ---- */
#define BL_SRAM_BASE           0x20000000u
#define BL_SRAM_SIZE           0x00020000u   /* 128 KiB */

/* ---- 行为常量 ---- */
#define BL_TRANSPORT_BT_EN     0       /* 本支持包未接蓝牙：通道 1 槽位保留（OTA_QUERY 通道号语义不变） */
#define BL_USE_HSE             1       /* 1=HSE->PLL 100MHz（默认）；0=HSI 16MHz 直驱（调试回退） */
#define BL_HSE_MHZ             25u     /* 板载晶振频率：8 或 25 两种均支持（编译期选择 PLL 参数，
                                          clock.c 内 #if 分支；8M: M=4/N=100，25M: M=25/N=200，
                                          均 VCO 200MHz、P=2 -> 100MHz） */
#define BL_BOOT_WAIT_MS        3000u   /* 启动等待窗口（ADR-004） */
#define BL_IWDG_TIMEOUT_MS     2000u   /* ADR-011 */
#define BL_IWDG_UPGRADE_TIMEOUT_MS 8000u  /* 升级擦写期放宽（ADR-015）：128K 扇区擦除 ~875ms（最大可
                                              翻倍）且单 bank 擦除期间 CPU 停顿无法喂狗——F4 头号设计点，
                                              8s 窗口由 wdg.c PR/256 真放宽实现 */
#define BL_UART_BAUD           115200u
#define BL_PROTOCOL_ACTIVE_MS  10000u  /* 协议活跃判定（ADR-008/009） */
#define BL_UI_REFRESH_MS       200u    /* 显示最低刷新间隔（display_led LED 模式驱动节拍） */
#define BL_RX_RING_SIZE        512u    /* architecture.md §6 */
#define BL_FRAME_DATA_MAX      256u    /* protocol.md §4 */
#define BL_FRAME_BYTE_TIMEOUT_MS 2000u /* 帧内字节间超时（protocol.md §4.2），与 F103 同值 */
#define BL_VERIFY_CHUNK        1024u   /* VERIFY 分块（喂狗粒度） */
#define BL_LOG_HEARTBEAT_MS    500u    /* 空闲心跳日志间隔（0=关闭；协议活跃期静默） */

/* ---- 引脚编号（gpio.c 内映射实际端口） ---- */
#define BL_PIN_LED             0u      /* PC13，低电平点亮（开漏灌电流，与 F103 方案一致） */
#define BL_PIN_BT_STATE        1u      /* PC14，本包 NC：未接 HC-05，OTA_QUERY 该字段读值无意义
                                          （协议 22B 响应结构保持不变，见 protocol.md §5.10） */

/* ---- F411 系统内存区（GET_INFO 用；RM0383/DS10697） ---- */
#define BL_UID_ADDR            0x1FFF7A10u   /* 96 位 Unique device ID 基址 */
#define BL_FLSIZE_ADDR         0x1FFF7A22u   /* Flash 容量寄存器（半字，KiB） */

#endif /* BOARD_CONFIG_H */
