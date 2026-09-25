# LiteBootLoader 总体设计（design）

> 版本 0.1.0 · 2026-09-25 · 状态：阶段 0 交付，待评审确认
> 关联文档：[architecture.md](architecture.md) · [partition.md](partition.md) · [protocol.md](protocol.md) · [external_interface.md](external_interface.md) · [versioning.md](versioning.md) · [vofa_plus.md](vofa_plus.md)
> 上位规则：本文件为 AGENTS.md（项目指令）阶段 0 交付物；与 AGENTS.md 冲突时以 AGENTS.md 为准。

## 1. 目标与范围

LiteBootLoader（BL）是一个面向 STM32F103C8T6 的可编译、可测试、可移植的裸机 BootLoader 框架：

- **当前范围**：启动决策、APP 合法性校验、USART1 升级、Flash 与参数区管理、OLED/LED 状态显示、IWDG、跳转 APP、配套 Python 工具与文档。
- **长期目标**：core 与 port 解耦，可迁移至 STM32F4 / G0 / H7。
- **非目标**（本期明确不做，接口预留见 §3 ADR-012）：联网 OTA 技术栈、外部 Flash、F103 双 APP 分区与自动回滚。

## 2. 需求到模块映射

| 需求（AGENTS.md 出处） | 承载模块 | 文档 |
|---|---|---|
| 启动决策与 APP 校验（§5.1） | `core/bl_boot.c` + `core/bl_metadata.c` | architecture.md §5、partition.md |
| USART1 升级协议（§6） | `core/bl_protocol.c` + `core/bl_transport.c` + F103 端口 `uart.c` | protocol.md |
| Flash 读写/擦除与地址防护（§5） | `core/bl_storage.c` + F103 端口 `flash.c` | partition.md §2 |
| 参数区双副本（§5） | `core/bl_metadata.c` | partition.md §4–§8 |
| OLED/LED 状态显示（§8） | `core/bl_ui.c` + `bsp/oled_ssd1306/` + 端口 `gpio.c`/`i2c.c` | architecture.md §8 |
| IWDG（§8.4） | 端口 `wdg.c` + 各耗时操作的喂狗点 | architecture.md §7 |
| 日志（§8.3） | `core/bl_log.c`（编译期开关） | design.md ADR-009 |
| 版本（§11） | `core/bl_version.h` | versioning.md |
| 上位机工具（§9.4） | `tools/python/bl_upgrade.py`（阶段 3） | protocol.md §9 |

## 3. 固化决策记录（ADR）

以下决策在阶段 0 固化，后续实现不得偏离；修改需走文档变更（`docs` 类型提交）并同步本表。

### ADR-001 APP 校验算法：CRC-32/ISO-HDLC（zlib 兼容）【用户已确认】

| 项 | 值 |
|---|---|
| 多项式 | 0x04C11DB7（反射实现 0xEDB88320） |
| 初值 / 终异或 | 0xFFFFFFFF / 0xFFFFFFFF |
| 输入/输出反射 | 是 / 是 |
| 标准校验值 | `"123456789"` → `0xCBF43926` |
| 实现方式 | MCU 侧 256 项查表软件 CRC（约 1 KiB 常量表） |

理由：Python `zlib.crc32` 直接兼容，上位机与测试零成本；软件查表实现成熟。**不使用** F1 硬件 CRC 单元：其固定非反射、仅 32 位字对齐，与字节流镜像不匹配，且需填充转换，收益为负。备选 CRC-32/MPEG-2（硬件对齐）已被否决，理由同上。

### ADR-002 帧校验算法：CRC-16/MODBUS

poly 0x8005（反射 0xA001），初值 0xFFFF，输入/输出反射，无终异或；标准校验值 `"123456789"` → `0x4B37`。CRC 覆盖 `VER` 至 `DATA` 末尾，小端发送（低字节在前）。参考实现见 [protocol.md](protocol.md) §4.1。

### ADR-003 升级模式停留等待【用户已确认】

进入升级模式后**无自动超时**：仅由 `JUMP_APP`、`RESET`、断电退出。理由：不存在"意外跳转半升级镜像"的路径，状态机与测试面最小；误入升级模式可由 `RESET`/断电自愈。备选"空闲 60s 自动跳转有效 APP"被否决，留作后续常量级增强（`BL_UPGRADE_TIMEOUT_MS`，默认 0=禁用）。

### ADR-004 启动决策与等待窗口

上电顺序与时钟策略见 ADR-010。读参数区最新有效副本后：

1. `bl_request == 1` → 消费并清除该标志（一次掉电安全写，见 partition.md §8）→ 进入升级模式。
2. 否则校验 APP：初始 MSP 在 `[0x20000000, 0x20005000)` 内、Reset Handler 在 APP 区且 Thumb 位（bit0）为 1、`app_size > 0`、CRC32 与参数区一致。
3. APP 有效 → 3 s 等待窗口（`BL_BOOT_WAIT_MS`，默认 3000，集中配置）：LED 慢闪，收到任意 CRC 有效帧即转入升级模式；窗口结束执行 §5.1 九步跳转。
4. APP 无效 → 进入升级模式（停留等待，ADR-003）。

### ADR-005 参数区双副本与掉电安全

页 62 / 页 63 各存一份元数据副本，字段布局、副本状态机（ERASED/VALID/INVALID）、seq 单调递增、交替写入、写后回读、断电恢复规则见 [partition.md](partition.md) §4–§7。禁止用"最后写有效标志"类含糊描述替代状态机。

### ADR-006 协议编号

帧格式、命令编号 0x01–0x09、响应 `CMD|0x80`、状态码 0x00–0x05、OTA 预留 0x10–0x1F，完整定义见 [protocol.md](protocol.md) §4–§6。协议版本字节 `VER=0x01` 独立于 BL 软件版本演进（见 versioning.md §4）。

### ADR-007 命令语义要点

- `WRITE_CHUNK` 写前自动检测目标页是否需要擦除（页内存在非 0xFF 字节则先擦，页间喂狗）；`ERASE_APP` 保留为显式整片擦除（快速路径）。二者均幂等。
- `VERIFY_APP` 携带 `app_size + app_crc32`，校验通过时 BL 以新 seq 自动持久化到参数区；擦除不更新元数据，失效安全依赖启动时 CRC 重算。
- 镜像必须以 0xFF 填充至 4 字节对齐后上传，`app_size` 为填充后长度，CRC 对填充后镜像计算。
- `JUMP_APP` 校验失败回 `STATE_ERROR` 不跳；成功回 `OK` 后延时 ~50 ms（保证主机收到响应）再执行九步跳转。
- 幂等性：所有命令重复执行无害（重写、重擦、重验），主机超时重试安全。

### ADR-008 LED 状态模式（PC13 低电平点亮）

| 状态 | 模式 |
|---|---|
| 等待升级（启动窗口 + 升级模式） | 慢闪 1 Hz（500 ms 亮 / 500 ms 灭） |
| 正在升级（协议活跃） | 快闪 5 Hz（100 ms 亮 / 100 ms 灭） |
| APP 无效或校验失败 | 双闪：200 亮-200 灭-200 亮-1400 灭（周期 2 s） |
| 即将跳转 APP | 常亮 ≥300 ms 后跳转 |
| 致命错误 | 三短闪：100-100-100-100-100-1500（周期 2 s） |

五种模式互不歧义（占空比/节拍不同）。判定"协议活跃"：最近 10 s 内收到过 CRC 有效的帧。

### ADR-009 日志与协议复用 USART1

日志默认与协议共用 USART1。规则：`protocol_active == 0` 时允许日志输出；收到 SOF 即静音，会话结束（跳转/复位/超 10 s 无帧）恢复；启动横幅仅在等待窗口输出。编译期控制：`BL_LOG_LEVEL`（ERROR/WARN/INFO/DEBUG，默认 INFO）、`BL_LOG_DISABLE=1` 全关。上位机解析不受影响：帧由 SOF/EOF 包裹并有 CRC 保护，混入的日志字节被解析器帧同步丢弃。另有**空闲心跳日志**（`BL_LOG_HEARTBEAT_MS`，调试期 500 ms，0 关闭）：协议空闲期周期输出 `I:hb <毫秒>`，用作 PA9 接线探针（逐脚轻触适配器 RXD，工具出现心跳文本即命中）与串口链路自检（干净文本=TX 与波特率正常；乱码=波特率/时钟异常；无输出=TX 路径或芯片被复位），协议活跃期同样静默。

### ADR-010 时钟与回退

**默认 HSI 8MHz 直驱**（`BL_USE_HSE=0`，2026-09-25 修订）：手焊板晶振频率/负载电容未验证前，HSE 不可信——某板曾用非 8MHz 晶振起振导致 PLL 输出偏离假设、UART 波特率整体错位（现象：心跳周期缩短、收发全乱码）。HSI 频率由厂商保证（±1%），8MHz 下 UART 误差 0.64%，完全可用。晶振确认为 8MHz 且能起振后，置 `BL_USE_HSE=1` 走 HSE→PLL×9=72MHz；HSE 起振等待 300ms，任一环节失败自动回退 HSI。回退/降级事件经日志（非升级会话期）上报，并在 OLED 状态区显示时钟源。USART 波特率始终按 `bl_clock_get_hz()` 实际值计算。

### ADR-011 IWDG 策略

IWDG 约 2 s（`BL_IWDG_TIMEOUT_MS` 默认 2000，集中配置），BL 启动即开启且**永不关闭**。喂狗点固定为：主循环顶部、ERASE 每页之间、VERIFY 每 1 KiB 块之间、OLED 每帧分片之间、JUMP 执行前。跳转后由 APP 接管喂狗（阶段 2 验证接管窗口，验收 §13.10）。

### ADR-012 端口抽象与 OTA 接入点

芯片差异收敛到 `port/`，通过 6 个 ops 结构提供：`bl_flash_ops` / `bl_uart_ops` / `bl_i2c_ops` / `bl_gpio_ops` / `bl_wdg_ops` / `bl_clock_ops`（方法清单见 [architecture.md](architecture.md) §3）。transport 抽象（init/send/recv）首实现为 UART，OTA 等 Broad 新通道只扩 transport，不动 protocol 核心；命令空间 0x10–0x1F 已预留（只声明，不实现）。

### ADR-013 编译器固定 AC5（用户决定，2026-09-25）

本项目统一使用 **ARM Compiler 5（ARMCC V5.06 update 7, build 960）**，不用 AC6：

- 工程内以 `uAC6=0` + `pCCUsed=5060960::V5.06 update 7 (build 960)::.\ARMCC` 显式固定（GUI/命令行一致）。
- 尺寸优化用 `-Ospace`（AC5 选项；**禁止** `-Oz` 等 AC6 专属选项进 Misc Controls）。
- CMSIS 6 已移除 AC5 支持，third_party/CMSIS 改用 V1.30 自包含内核头（`core_cm3.h`+`core_cm3.c`+器件头，来源与许可见 third_party/CMSIS/LICENSES.md）。
- 备选 AC6（曾在阶段 1 编译通过，bin 8 824 B）作为迁移路径保留；切换时需同步更换 third_party 头并更新本 ADR。

## 4. 默认假设确认表（AGENTS.md §15）

| # | 假设 | 状态 | 固化位置 |
|---|---|---|---|
| 1 | USART1 PA9/PA10，115200 8N1 | 确认 | protocol.md §3 |
| 2 | LED PC13 低电平点亮 | 确认 | design.md ADR-008 |
| 3 | OLED SSD1306，PB9=SDA / PB8=SCL，软件 I2C，保留硬件 I2C1 重映射选项 | 确认 | architecture.md §3、external_interface.md §2 |
| 4 | IWDG 约 2 s | 确认（2000 ms） | design.md ADR-011 |
| 5 | APP 基址 0x08004000 | 确认 | partition.md §1 |
| 6 | APP 校验 CRC32 | 固化为 ISO-HDLC | ADR-001 |
| 7 | 升级帧校验 CRC16/MODBUS | 固化 | ADR-002 |

## 5. 集中配置项（阶段 1 落地到 `board_config.h`）

以下常量全项目唯一出处为 `board_config.h`（默认值列在此处，禁止散落硬编码）：

| 常量 | 默认值 | 说明 |
|---|---|---|
| `BL_BOOT_WAIT_MS` | 3000 | 启动等待窗口 |
| `BL_IWDG_TIMEOUT_MS` | 2000 | IWDG 超时 |
| `BL_UART_BAUD` | 115200 | 升级串口波特率 |
| `BL_APP_BASE` / `BL_APP_SIZE` | 0x08004000 / 0x0000B800 | APP 分区 |
| `BL_PARAM_BASE` / `BL_PARAM_SIZE` | 0x0800F800 / 0x00000800 | 参数区 |
| `BL_PROTOCOL_ACTIVE_MS` | 10000 | 协议活跃判定窗口 |
| `BL_UI_REFRESH_MS` | 200 | OLED 最低刷新间隔 |
| `BL_LOG_LEVEL` / `BL_LOG_DISABLE` | INFO / 0 | 日志编译期控制 |
| `BL_LOG_HEARTBEAT_MS` | 500 | 空闲心跳日志间隔（0=关闭；调试期探针值） |
