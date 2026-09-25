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

/* ---- SRAM（跳转校验范围） ---- */
#define BL_SRAM_BASE           0x20000000u
#define BL_SRAM_SIZE           0x00005000u   /* 20 KiB */

/* ---- 行为常量 ---- */
#define BL_USE_HSE             1       /* 1=HSE 8M->PLL 72M（默认：8MHz 晶振已由 ST 标准工程在本板实测 72M 可用）；
                                          0=HSI 8MHz 直驱（晶振异常时的调试回退，UART 115200 仍可用） */
#define BL_BOOT_WAIT_MS        3000u   /* 启动等待窗口（ADR-004） */
#define BL_IWDG_TIMEOUT_MS     2000u   /* ADR-011 */
#define BL_UART_BAUD           115200u
#define BL_PROTOCOL_ACTIVE_MS  10000u  /* 协议活跃判定（ADR-008/009） */
#define BL_UI_REFRESH_MS       200u    /* OLED 最低刷新间隔 */
#define BL_RX_RING_SIZE        512u    /* architecture.md §6 */
#define BL_FRAME_DATA_MAX      256u    /* protocol.md §4 */
#define BL_FRAME_BYTE_TIMEOUT_MS 2000u /* 帧内字节间超时（protocol.md §4.2）。2026-09-26 定版：
                                          假触发根因是 poll 用循环顶旧时间戳做无符号减法、毫秒边界
                                          跨越时回绕（selftest 遥测实锤），已改现场重读时刻；阈值
                                          取 2s 容忍 USB/CDC 转发抖动，主机重试间隔需 ≥2s */
#define BL_VERIFY_CHUNK        1024u   /* VERIFY 分块（喂狗粒度） */
#define BL_LOG_HEARTBEAT_MS    500u    /* 空闲心跳日志间隔（0=关闭；协议活跃期静默；接线探针模式） */

/* ---- 引脚编号（gpio.c 内映射实际端口） ---- */
#define BL_PIN_LED             0u      /* PC13，低电平点亮 */
#define BL_PIN_I2C_SCL         1u      /* PB8 */
#define BL_PIN_I2C_SDA         2u      /* PB9 */

/* ---- F103 系统内存区（GET_INFO 用） ---- */
#define BL_UID_ADDR            0x1FFFF7E8u
#define BL_FLSIZE_ADDR         0x1FFFF7E0u

#endif /* BOARD_CONFIG_H */
