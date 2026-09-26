# LiteBootLoader 架构设计（architecture）

> 版本 0.1.0 · 2026-09-25 初版 · 2026-09-26 修订 · 状态：与实现同步（阶段 4 收尾）
> 关联：[design.md](design.md)（固化决策） · [partition.md](partition.md)（Flash/元数据） · [protocol.md](protocol.md)（通信协议） · [external_interface.md](external_interface.md)（对外接口索引）

## 1. 分层总览

```text
┌────────────────────────────────────────────────────────────┐
│                       tools/（主机侧）                       │
│   uvprojx · ico · vofa+；上位机见独立仓 LiteBootUpgrader      │
└──────────────────────────────┬─────────────────────────────┘
                               │ USART1（帧协议，protocol.md）
┌──────────────────────────────▼─────────────────────────────┐
│  core/（纯 C，无 HAL、无芯片头文件，可主机侧单测）             │
│   bl_core    总状态机与命令分发                              │
│   bl_protocol 帧解析/组帧/CRC16/超时                         │
│   bl_storage Flash 抽象操作 + APP 擦写/CRC32                 │
│   bl_metadata 参数区双副本（状态机见 partition.md）           │
│   bl_boot    启动决策 / APP 校验 / 九步跳转                   │
│   bl_transport init/send/recv 抽象（首实现 UART）            │
│   bl_ui / bl_log 已服务化迁出（ADR-014）                      │
│   bl_crc / bl_version + core/bl_display.h · bl_debug.h 接口  │
├──────────────── services/（可替换服务实现，ADR-014）─────────┤
│   display_oled  OLED+LED 状态指示（实现 bl_display_ops）      │
│   debug_uart    USART1 日志（实现 bl_debug_ops）             │
├──────────────── ops 结构（§3）──────────────────────────────┤
│  port/stm32f1/f103c8t6/（芯片相关，可替换）                   │
│   flash.c uart.c i2c.c gpio.c wdg.c clock.c systick.c       │
├────────────────────────────────────────────────────────────┤
│  bsp/oled_ssd1306/（板级器件驱动，经 bl_i2c_ops 通信）        │
├────────────────────────────────────────────────────────────┤
│  CMSIS（third_party/）                                      │
└────────────────────────────────────────────────────────────┘
```

依赖规则（编译期可验证）：

1. `core/**` 不得 `#include` STM32/HAL/CMSIS 头文件，只能依赖 `port/bl_port.h` 暴露的 ops 接口。
2. `port/**` 不依赖 `core/**` 的业务逻辑，只实现 ops。
3. `bsp/**` 只依赖 `bl_i2c_ops`/`bl_gpio_ops` 抽象与自身配置，不直接访问寄存器。
4. 无 RTOS、无 `malloc/calloc/realloc`，全部静态分配（RAM 预算见 §9）。

## 2. 目录职责

| 目录 | 职责 | 说明 |
|---|---|---|
| `core/` | 状态机、协议、启动策略、元数据、CRC、UI 调度、日志 | 纯逻辑，可被主机侧单元测试编译（`BL_HOST_TEST` 分支 mock ops） |
| `port/stm32f1/f103c8t6/` | 首个端口实现 | `port/stm32f4、stm32g0、stm32h7/` 预留空目录 |
| `bsp/oled_ssd1306/` | SSD1306 初始化/绘字/分片刷新 | 经 `bl_i2c_ops`，非阻塞 |
| `app/examples/f103c8t6_app/` | APP 示例（阶段 2） | 链接 `0x08004000`、设 VTOR、接管 IWDG、可请求进 BL |
| `linker/` | `bootloader.ld` / `app.ld` / `bootloader.sct` | 阶段 1 交付 |
| `tools/` | 主机侧工具 | 已交付 uvprojx/ico；python 升级器与 vofa+ 配置为阶段 3 |
| `docs/`、`scripts/`、`third_party/` | 文档、构建脚本、CMSIS | — |

## 3. 端口层 ops 接口

接口在 `port/bl_port.h` 定义（阶段 1 落地），F103 实现提供实例。方法签名先行固化：

```c
typedef struct {
    void    (*init)(void);
    bool    (*read)(uint8_t *buf, uint32_t len, uint32_t *out_len);   /* 非阻塞，从环形缓冲取 */
    bool    (*write)(const uint8_t *buf, uint32_t len);               /* 阻塞发送 */
} bl_uart_ops;

typedef struct {
    void    (*init)(void);
    bool    (*read)(uint32_t addr, uint8_t *buf, uint32_t len);       /* 任意对齐读 */
    bool    (*write)(uint32_t addr, const uint8_t *data, uint32_t len); /* 半字对齐，自动补 0xFF */
    /* 擦除单元（ADR-015，替代旧"页"抽象）：F1 均匀 1K 页；F4 非均匀扇区按表查询 */
    uint32_t (*unit_count)(void);                                     /* 全 Flash 擦除单元数 */
    uint32_t (*unit_addr)(uint32_t unit_index);                       /* 单元起始地址（越界返回 0） */
    uint32_t (*unit_size)(uint32_t unit_index);                       /* 单元大小（越界返回 0） */
    bool    (*erase_unit)(uint32_t unit_index);                       /* 擦除一个擦除单元 */
    bool    (*is_range_valid)(uint32_t addr, uint32_t len);           /* 分区边界检查 */
} bl_flash_ops;

typedef struct {
    void    (*init)(void);
    bool    (*write_mem)(uint8_t dev_addr, uint8_t reg, const uint8_t *data, uint16_t len);
    bool    (*read_mem)(uint8_t dev_addr, uint8_t reg, uint8_t *buf, uint16_t len);
} bl_i2c_ops;

typedef struct {
    void    (*init)(void);
    void    (*write)(uint8_t pin_id, bool level);
    void    (*toggle)(uint8_t pin_id);
    bool    (*read)(uint8_t pin_id);
} bl_gpio_ops;

typedef struct {
    void    (*init)(uint32_t timeout_ms);
    bool    (*set_timeout_ms)(uint32_t timeout_ms);   /* 运行时重配（ADR-015 升级期放宽） */
    void    (*refresh)(void);
} bl_wdg_ops;

typedef struct {
    void    (*init)(void);                 /* HSE→PLL 72MHz，失败回退 HSI（ADR-010） */
    uint32_t (*sysclk_hz)(void);
    uint32_t (*tick_ms)(void);             /* SysTick 毫秒计数 */
    void    (*delay_ms)(uint32_t ms);
} bl_clock_ops;
```

约定：所有 ops 返回 `bool` 表示成败；`bl_flash_ops.write/erase_unit` 由 `bl_storage` 层先做地址合法性检查（[partition.md](partition.md) §2 矩阵），ops 内部再做二次防御检查（物理边界由 board_config 定界）。core 不假设擦除单元等大（ADR-015）。

除 ops 结构外，`bl_port.h` 另提供跳转序列与系统级辅助函数（供 `bl_boot` 九步跳转使用，见 §5）：

```c
void bl_port_disable_irq(void);
void bl_port_stop_systick(void);
void bl_port_uart_deinit(void);
void bl_port_i2c_release(void);          /* PB8/PB9 恢复浮空模拟态 */
void bl_port_clear_pending_irqs(void);   /* NVIC ICPR 全清 */
void bl_port_set_vtor(uint32_t addr);
void bl_port_set_msp(uint32_t value);
void bl_port_jump(uint32_t reset_handler);
void bl_port_system_reset(void);
uint32_t bl_port_read_word(uint32_t addr);
```

## 4. 运行时模型

裸机超级循环 + SysTick 1 ms 节拍，无 RTOS：

```c
/* main 循环骨架（阶段 1 实现，此处为结构示意） */
while (1) {
    bl_wdg_ops.refresh();                 /* 喂狗点 1：主循环顶 */
    bl_display.tick(bl_clock_ops.tick_ms());  /* LED 模式 + OLED 限频分片刷新，非阻塞 */
    n = bl_uart_ops.read(rx_chunk, ...);  /* 非阻塞取 RX 环形缓冲 */
    bl_protocol_feed(rx_chunk, n);        /* 帧解析 → 完整帧交 bl_core 分发 */
    bl_core_poll();                       /* 状态机推进（等待窗口计时等） */
}
```

非阻塞原则：任何单次操作不得长时间霸占主循环——Flash 擦除按页拆分、CRC 校验按 1 KiB 块拆分、OLED 刷新按竖向条带分片，分片间回主循环（并喂狗，见 §7）。

## 5. BL 主状态机

| 状态 | 含义 | 出口 |
|---|---|---|
| `INIT` | 时钟/IWDG/串口/LED/OLED/参数区初始化 | → `BOOT_DECISION` |
| `BOOT_DECISION` | 读参数区副本，按 ADR-004 判定 | → 消费 bl_request / → `WAIT_HOST` / → `UPGRADE_WAIT` |
| `WAIT_HOST` | APP 有效，3 s 等待窗口（LED 慢闪，可输出启动横幅日志） | 超时 → `JUMPING`；收到 CRC 有效帧 → `UPGRADE_WAIT` |
| `UPGRADE_WAIT` | 停留等待主机命令（ADR-003，无超时） | 命令执行返回本状态；`JUMP_APP` 校验通过 → `JUMPING`；`RESET` → 复位 |
| `ERASING` / `WRITING` / `VERIFYING` | 命令执行的瞬时标记（供 LED 快闪与 OLED 进度），单命令内部完成 | 完成 → `UPGRADE_WAIT` |
| `JUMPING` | §5.1 九步跳转执行 | → APP |
| `FAULT` | 致命错误（Flash 驱动失败等），LED 三短闪 | 仅 IWDG 复位或手动断电 |

说明：升级流程为**命令驱动**，无独立"会话状态机"；`ERASE_APP` 与 `WRITE_CHUNK` 无强制先后（写前确保擦除态——位图 + 扫描兜底，同页分块写入不互抹，ADR-007），`VERIFY_APP` 通过后自动持久化元数据。

## 6. 中断与缓冲

| 中断 | 用途 | 说明 |
|---|---|---|
| SysTick | 1 ms 节拍、超时/窗口计时 | 仅递增毫秒计数，不做业务 |
| USART1_RX | 收帧字节流 → 512 B 环形缓冲 | 帧最长 267 B（2+1+1+1+2+256+2+2）；@115200 约 11.5 B/ms，单页擦除（≤40 ms）期间的到达字节 ≤460 B，512 B 环形缓冲可吸收单页突发。更长的连续到达依赖主机"停等"协议（发出命令后等响应），溢出字节按帧同步丢弃处理（protocol.md §4.2） |
| USART1_TX | 不用中断 | 响应/日志帧短，阻塞发送（≤272 B ≈ 24 ms @115200，可接受），文档化取舍 |

主循环内联执行耗时 Flash 操作时，RX 中断继续填充环形缓冲，不丢字节（缓冲深度按上表核算）。

## 7. IWDG 喂狗点分布（固定清单）

1. 主循环顶部（保证 < 2 s 周期）；
2. `ERASE_APP` / 自动擦页：**每页之间**；
3. `VERIFY_APP`：**每 1 KiB 块之间**；
4. OLED 刷新：**每条带分片之间**；
5. `JUMPING` 执行九步前（喂狗后立即跳转，APP 接管窗口见验收 §13.10）。

禁止在以外的位置随意加喂狗掩盖长阻塞；新增耗时操作必须先在此清单登记喂狗点。

## 8. 显示/调试服务模型（ADR-014）

- `bl_display.tick(now_ms)` 每次主循环调用，内部按 `BL_UI_REFRESH_MS`（默认 200 ms）限频。
- 显示服务实现于 `services/display_oled/`（`bl_display_ops`，OLED+LED 状态指示），语义状态由 core 注入（`set_state/set_progress/set_crc_ok`）。
- LED：由 BL 状态 + 协议活跃标志查 ADR-008 模式表驱动，无阻塞延时。
- OLED：显示 BL 版本、芯片型号、APP 状态（有效/无效/大小/CRC）、升级进度（ERASE/WRITE/VERIFY 百分比）、CRC 状态、IWDG 状态（运行中时钟源）；刷新按竖向条带分片（如 8 列/片）经 `bl_i2c_ops` 发送，分片间返回。
- 用户自检页面：`bl_display_user_page()` **弱符号**（空实现，`bl_display.h` 声明），`BL_DISPLAY_USER_PAGE=1` 时在等待/升级模式替代标准页。
- 调试服务实现于 `services/debug_uart/`（`bl_debug_ops`，USART1，协议活跃期静音）；core 侧仅用 `BL_LOGx` 宏，换通道只换实现。

## 9. 内存预算（2026-09-26 AC5 实测回填，ARMCC V5.06u7 `-Ospace`，ADR-014 服务化后）

| 项 | 预算/实测 | 说明 |
|---|---|---|
| RAM：RX 环形缓冲 | 512 B | §6 |
| RAM：OLED 帧缓冲 | 1 KiB | 128×64/8 |
| RAM：VERIFY 块缓冲 | 1 KiB | bl_storage 静态分配 |
| RAM：栈 | 1 KiB | 启动文件 Stack_Size |
| RAM：ZI 合计实测 | 3 888 B（含上列） | 20 KiB 上限的 19% |
| Flash：CRC32 常量表 | ≈ 1 KiB | ADR-001（在 RO-data 内） |
| Flash：实测 Code=11 460 + RO=1 944 + RW=136 | **13 540 B ≈ 13.2 KiB** | **≤ 16 KiB 验收线 ✓**（ADR-015 擦除单元抽象 + IWDG 放宽后；bin SHA `e864fe22…`；AC6 -Oz 时为 8 824 B，供参考） |
| APP .bin | **8 064 B，≤ 46 KiB（验收线）✓** | 阶段 2，CSP A 同步重建（SHA `a7a8a647…`） |
