# LiteBootLoader 总体设计（design）

> 版本 0.2.0 · 2026-09-25 初版 · 2026-09-26/27 修订 · 状态：与实现同步
> 关联文档：[../architecture.md](../architecture.md) · [../partition.md](../partition.md) · [../protocol.md](../protocol.md) · [../external_interface.md](../external_interface.md) · [versioning.md](versioning.md) · [vofa_plus.md](vofa_plus.md)
> 上位规则：本文件为 ../AGENTS.md（项目指令）阶段 0 交付物；与 ../AGENTS.md 冲突时以 ../AGENTS.md 为准。

## 1. 目标与范围

LiteBootLoader（BL）是一个面向 STM32F103C8T6 的可编译、可测试、可移植的裸机 BootLoader 框架：

- **当前范围**：启动决策、APP 合法性校验、USART1/蓝牙（HC-05）双通道升级、OTA 状态查询、Flash 与参数区管理、OLED/LED 状态显示、IWDG、跳转 APP、配套 Python 工具与文档。
- **长期目标**：core 与 port 解耦，可迁移至 STM32F4 / G0 / H7。
- **非目标**（本期明确不做，接口预留见 §3 ADR-012/ADR-016）：联网 OTA 技术栈（蓝牙 SPP 字节流通道已于 0.2.0 接入、WIFI 仅 API 预留）、外部 Flash、F103 双 APP 分区与自动回滚、**固件签名/认证**（review P3：升级链路仅 CRC16/CRC32 完整性校验，任意主机均可烧写；认证的接入点应落在 storage 校验链与元数据结构，见 ../partition.md §4）。

## 2. 需求到模块映射

| 需求（../AGENTS.md 出处） | 承载模块 | 文档 |
|---|---|---|
| 启动决策与 APP 校验（§5.1） | `core/bl_boot.c` + `core/bl_metadata.c` | ../architecture.md §5、../partition.md |
| USART1 升级协议（§6） | `core/bl_protocol.c` + `core/bl_transport.c` + F103 端口 `uart.c` | ../protocol.md |
| 蓝牙空口升级 + OTA 查询（规划书《空口蓝牙串口及OTA》2026-09-27，ADR-016） | F103 端口 `uart2.c` + `core/bl_transport.c` 通道注册表 + `core/bl_core.c` OTA_QUERY | ../architecture.md §6.1、../protocol.md §5.10、[bluetooth_notes.md](bluetooth_notes.md) |
| F411CEU6 最小支持包（ADR-017，仅串口+引导） | `chips/f411ceu6.json` + `port/stm32f4/f411ceu6/` + 服务 `services/display_led/` | ../porting_guide.md §2、../architecture.md §6.1、../partition.md §1 |
| Flash 读写/擦除与地址防护（§5） | `core/bl_storage.c` + F103 端口 `flash.c` | ../partition.md §2 |
| 参数区双副本（§5） | `core/bl_metadata.c` | ../partition.md §4–§8 |
| OLED/LED 状态显示（§8） | 服务 `services/display_oled/`（ADR-014）+ `bsp/oled_ssd1306/` + 端口 `gpio.c`/`i2c.c` | ../architecture.md §8 |
| IWDG（§8.4） | 端口 `wdg.c` + 各耗时操作的喂狗点 | ../architecture.md §7 |
| 日志（§8.3） | 服务 `services/debug_uart/`（ADR-014，编译期开关） | design.md ADR-009 |
| 版本（§11） | `core/bl_version.h` | versioning.md |
| 上位机工具（§9.4） | 独立仓 `LiteBootUpgrader`（`../LiteBootUpgrader/`，v1.1.0 含 GUI） | ../protocol.md §9 |

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

poly 0x8005（反射 0xA001），初值 0xFFFF，输入/输出反射，无终异或；标准校验值 `"123456789"` → `0x4B37`。CRC 覆盖 `VER` 至 `DATA` 末尾，小端发送（低字节在前）。参考实现见 [../protocol.md](../protocol.md) §4.1。

### ADR-003 升级模式停留等待【用户已确认】

进入升级模式后**无自动超时**：仅由 `JUMP_APP`、`RESET`、断电退出。理由：不存在"意外跳转半升级镜像"的路径，状态机与测试面最小；误入升级模式可由 `RESET`/断电自愈。备选"空闲 60s 自动跳转有效 APP"被否决，留作后续常量级增强（`BL_UPGRADE_TIMEOUT_MS`，默认 0=禁用）。

### ADR-004 启动决策与等待窗口

上电顺序与时钟策略见 ADR-010。读参数区最新有效副本后：

1. `bl_request == 1` → 消费并清除该标志（一次掉电安全写，见 ../partition.md §8）→ 进入升级模式。
2. 否则校验 APP：初始 MSP 在 `[0x20000000, 0x20005000)` 内、Reset Handler 在 APP 区且 Thumb 位（bit0）为 1、`app_size > 0`、CRC32 与参数区一致。
3. APP 有效 → 3 s 等待窗口（`BL_BOOT_WAIT_MS`，默认 3000，集中配置）：LED 慢闪，收到任意 CRC 有效帧即转入升级模式；窗口结束执行 §5.1 九步跳转。
4. APP 无效 → 进入升级模式（停留等待，ADR-003）。

### ADR-005 参数区双副本与掉电安全

页 62 / 页 63 各存一份元数据副本，字段布局、副本状态机（ERASED/VALID/INVALID）、seq 单调递增、交替写入、写后回读、断电恢复规则见 [../partition.md](../partition.md) §4–§7。禁止用"最后写有效标志"类含糊描述替代状态机。

### ADR-006 协议编号

帧格式、命令编号 0x01–0x09、响应 `CMD|0x80`、状态码 0x00–0x05、OTA 预留 0x10–0x1F，完整定义见 [../protocol.md](../protocol.md) §4–§6。协议版本字节 `VER=0x01` 独立于 BL 软件版本演进（见 versioning.md §4）。

### ADR-007 命令语义要点

- `WRITE_CHUNK` 写前确保目标页处于擦除态：storage 层维护"本会话已擦页位图"（`ERASE_APP` 逐页置位；复位后清零、由内容扫描兜底），仅对未标记页执行"扫描 + 按需擦除"，同一页的后续分块写入不再触发整页擦除——旧实现会把页内先前分块抹掉（2026-09-25 升级流程 selftest 实测抓到：页内仅最后一次写入存活，verify 回读 CRC 逐轮一致实锤）。页间喂狗。`ERASE_APP` 保留为显式整片擦除（快速路径）。二者均幂等（同址重写同值不报 PGERR）。
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

**默认 HSE 8MHz→PLL 72MHz**（`BL_USE_HSE=1`，2026-09-25 二次修订）：此前"默认 HSI 直驱"的依据（晶振未验证、"尝试 HSE 会破坏 UART"）已被排障推翻——真正根因是 SystemInit 在 `__main` scatter 清零前写静态 `s_hse_ok`，被初值覆盖后系统按 8M 配置 BRR/SysTick/I2C 延时，而芯片实跑 72M（对照证据：ST 标准工程在本板实测可进 72M，晶振与硬件无任何问题）。修复后 `bl_clock_port_init()` 改读硬件 SWS 位判定真实时钟源，SystemInit 兑现"只写寄存器"约束。`BL_USE_HSE=0` 保留为晶振异常时的调试回退（HSI 8MHz，UART 误差 0.64%）。HSE 起振等待 300ms，任一环节失败自动回退 HSI；回退事件经日志（非升级会话期）上报，并在 OLED 状态区显示时钟源。USART 波特率、软件 I2C 延时、SysTick 均按 `bl_clock_get_hz()` 实际值派生。2026-09-25 23:15 硬件复验：BL@72M 串口链路全通（心跳干净、PING 响应与 45cc2b4 黄金帧逐字节一致），OLED 显示 CLK:72M。

### ADR-011 IWDG 策略

IWDG 约 2 s（`BL_IWDG_TIMEOUT_MS` 默认 2000，集中配置），BL 启动即开启且**永不关闭**。喂狗点固定为：主循环顶部、ERASE 每页之间、VERIFY 每 1 KiB 块之间、OLED 每帧分片之间、JUMP 执行前。跳转后由 APP 接管喂狗（阶段 2 验证接管窗口，验收 §13.10）。

### ADR-012 端口抽象与 OTA 接入点

芯片差异收敛到 `port/`，通过 6 个 ops 结构提供：`bl_flash_ops` / `bl_uart_ops` / `bl_i2c_ops` / `bl_gpio_ops` / `bl_wdg_ops` / `bl_clock_ops`（方法清单见 [../architecture.md](../architecture.md) §3）。transport 抽象（init/send/recv）首实现为 UART，OTA 等 Broad 新通道只扩 transport，不动 protocol 核心；命令空间 0x10–0x1F 已预留（只声明，不实现）。

### ADR-013 编译器固定 AC5（用户决定，2026-09-25）

本项目统一使用 **ARM Compiler 5（ARMCC V5.06 update 7, build 960）**，不用 AC6：

- 工程内以 `uAC6=0` + `pCCUsed=5060960::V5.06 update 7 (build 960)::.\ARMCC` 显式固定（GUI/命令行一致）。
- 尺寸优化用 `-Ospace`（AC5 选项；**禁止** `-Oz` 等 AC6 专属选项进 Misc Controls）。
- CMSIS 6 已移除 AC5 支持，third_party/CMSIS 改用 V1.30 自包含内核头（`core_cm3.h`+`core_cm3.c`+器件头，来源与许可见 third_party/CMSIS/LICENSES.md）。
- 备选 AC6（曾在阶段 1 编译通过，bin 8 824 B）作为迁移路径保留；切换时需同步更换 third_party 头并更新本 ADR。

### ADR-014 显示/调试服务化解耦（用户决定，2026-09-26）

将显示（OLED+LED）与调试（日志）从 core 解耦为**独立服务模块**，core 只依赖统一 API：

- 接口头 `core/bl_display.h`（`bl_display_ops`：init/set_state/set_progress/set_crc_ok/tick）
  与 `core/bl_debug.h`（`bl_debug_ops`：init/set_level/get_level/log + `BL_LOGx` 兼容宏），
  绑定风格与 port ops 一致（链接期单实现）。
- 实现：`services/display_oled/`（OLED 页渲染 + LED 模式表，语义沿用 ADR-008）与
  `services/debug_uart/`（USART1 日志，ADR-009 静音策略在实现内）。
- 依赖方向：services → core/port/bsp 允许；core 禁止 include services/bsp。
- 用户自检页扩展点落地为 `bl_display_user_page()` 弱符号（AGENTS §8.1），
  `BL_DISPLAY_USER_PAGE=1` 时在等待/升级模式替代标准页。
- 备选实现（LED-only 显示、RTT 日志）换 services 目录即可，core 不动。

### ADR-017 F411CEU6 最小支持包（用户确认，2026-09-29）

第二个芯片支持包（CSP 阶段 B，阶段 A 见 ADR-015），**仅串口升级 + 引导跳转**：

- **最小包边界**：无 OLED/I2C/蓝牙/签名。显示走新服务 `services/display_led`
  （LED-only，模式表沿用 ADR-008）；transport 通道 1 经 `BL_TRANSPORT_BT_EN=0`
  关闭注册（槽位保留，OTA_QUERY 通道号语义不变，见 b14ed08）；OTA_QUERY 等核心
  命令随 main 自带不裁剪。
- **分区**（512K = 4×16K + 64K + 3×128K，RM0383 §3.3）：BL=扇区 0-1（32K）、参数区=
  扇区 2/3（双副本各 16K 独立擦除单元）、APP=扇区 4-7（448K @ 0x08010000）。
  BL 取 32K 而非 16K：F103 BL 已 15.3K，F4 版留足余量。
- **时钟**：`BL_HSE_MHZ` 编译期支持 8 与 25 两种晶振（均 →100MHz、3WS）；100MHz
  前置 PWR VOS=Scale 1（默认 Scale 2 上限 84MHz）；HSE 失败回退 HSI 16MHz。
- **IWDG**：2000ms/放宽 **8000ms**——128K 扇区擦除典型 ~875ms（最大可翻倍）且单
  bank 擦除期间 CPU 停顿无法喂狗，是 F4 移植头号设计点；wdg.c 以 PR=/256 一档覆盖
  两档（reload 250/1000 ≤4095）。
- **CMSIS F4**：Core(M) V5.6.0（保留 cmsis_armcc.h，AC5 兼容）+ Device STM32F4xx，
  均自本机 STM32Cube_FW_F4_V1.28.3 原样拷贝（Apache-2.0，SHA-256 见
  ../third_party/CMSIS/LICENSES.md）；F1 侧 V1.30 不动。startup 改编件放 port 目录
  （去堆，与 F1 同处置）。新增 `scripts/vendor_copy.py` 支撑原样拷贝+哈希核验。
- **产物槽位**：新芯片 spec/sct/uvprojx 入 `chips/<id>/` 与 `linker/<id>/`
  （`build.artifact_dir/sct_dir`），工程路径经 `build.proj_rel` 前缀相对工程目录
  解析；f103c8t6 保持仓库根 legacy 槽位，渲染产物逐字节不变（roundtrip 强制）。
- **版本策略（用户决定）**：仅追加 CSP、不触碰 core/协议/固件行为 → **不升 BL
  版本号**（不改 bl_version.h），CHANGELOG 走 `[Unreleased]`；动 core/协议的迭代
  （如下述签名）才升版。
- **签名/哈希校验**（用户确认独立迭代）：本期不做。验签链属 core 级特性（元数据
  结构+启动决策链+协议+上位机+签名工具），接入点见 ../partition.md §4；F411 参数区
  每副本 16K、BL 32K（现 12 452 B）预算充足，后加无需改分区；F103 BL 16K 装不下，
  届时以编译开关裁剪并文档明示无认证能力。
- **验证深度**：编译 + 一致性测试通过即交付（用户确认）；蓝牙 N/A、OLED N/A，
  硬件在环（烧录/升级/跳转/断电演练）未执行，见 dev/test_plan.md。

### ADR-016 多通道 transport + OTA_QUERY（用户确认，2026-09-27）

规划书《空口蓝牙串口及OTA》三目标落地：

- **蓝牙通道**（目标 1/2）：HC-05（BT 2.0 SPP）→ USART2 PA2/PA3 为 transport 通道 1；
  STATE→PB0（连接指示，输入下拉）、EN→PB1（默认低，运行时翻转进 AT 模式不可靠，仅
  预留——联网核实结论见 [bluetooth_notes.md](bluetooth_notes.md) §2）。数据模式一次性
  AT 配置为 115200（`BL_BT_UART_BAUD`，与 USART1 同速），固件不做运行时 AT。
- **通道注册表与仲裁**（目标 2）：`bl_transport` 改为通道表 + 活动通道锁（静默 2 s 释放，
  与帧内字节超时同窗，见 ../architecture.md §6.1），protocol 层无感；WIFI 为同签名占位
  stub（目标 2 只预留实现层与 API，实接入补实现）。`bl_transport.c` 顺带修正直接 include
  芯片端口头的分层破绽（统计函数改经 bl_port.h 声明）。
- **OTA 命令**（目标 3，轻量）：0x10 OTA_QUERY 只读查询（BL/APP 版本、APP 实时有效性、
  元数据 seq、请求到达通道、BT 连接状态，../protocol.md §5.10）；升级复用既有幂等命令；
  0x11–0x1F 继续预留。备选"完整 OTA BEGIN/DATA/END 状态机"被否决（与 WRITE_CHUNK 机制
  重叠、16 KiB 余量不容，用户确认轻量方案）。
- 体积影响：BL 13 728 → 15 324 B（16 KiB 上限内，余量 ~1 KiB）；APP 8 072 → 8 152 B
  （共享 uart.c/gpio.c 随 BT 引脚与弱符号微增）。

### ADR-015 多芯片支持：CSP + manifest 驱动（用户决定，2026-09-27）

目标：让"加一颗芯片"变得便捷（建目录 + chip.json + 实现 ops，构建零手工）。结构方案
2026-09-26 定稿：**芯片支持包（CSP）+ manifest 驱动，单一主线**；否决每芯片一个分支
（主线分裂、公共修复需 N 处 cherry-pick）。分支仅保留两个用途：芯片 bring-up 临时分支、
按板发布 tag（如 `f103c8t6-v0.1.0`）。阶段 A（本次）只做基础设施，F103 全程回归锚定：

- **chip.json 清单**（`chips/<id>.json`，构建侧事实源）：device（含 DFP flash_driver/
  register_file/sfd_file/cputype）、build（defines/include/文件清单）、memory、partitions
  （含参数双副本单元）、erase_units（F1 均匀页紧凑描述；F4 非均匀表式扩展位）、clock、
  pins、iwdg（normal_ms + **upgrade_relaxed_ms**）、sysmem。与 C 侧唯一出处
  `board_config.h` 的四类常量一致性由 `chips/test_chip.py` 强制。
- **模板化生成**：LiteTools 仓（`../LiteTools`）的 `uvprojx/chipfill.py`（占位符
  `{{chip.x}}` / `{"$chip": ...}`）+ 主仓 `chips/templates/*.spec.template.json`（项目
  工程结构数据）+ LiteTools `uvprojx/templates/*.sct.template`（通用形状，随工具分发）
  → per-chip `.spec.json` 与 `.sct`（生成产物入库）。generator.py 移除全部设备名硬编码
  （FlashDriverDll/RegisterFile/SFDFile/AdsCpuType/-pCM3/LDads 地址均来自规格 device 字段）。
  金标准：渲染 spec 与原手写 spec 语义等价；改造后 F103 BL bin 与基线**逐字节一致**
  （12 972 B，`f08f6d46…`）。`build_keil.sh` 参数化 `CHIP=<id>` 并补齐 app 目标。
- **擦除单元抽象**（core 唯一计划内变更）：`bl_flash_ops` 的 `erase_page/page_size/
  erase_range` 升级为 `unit_count/unit_addr/unit_size/erase_unit`（`erase_range` 无调用方，
  移除）；F1 = 64×1K 均匀页，F4 = 非均匀扇区表。bl_storage/bl_metadata 全部按单元迭代；
  bl_metadata 新增**双副本必须落在两个独立擦除单元**的加载期校验（掉电安全前提）。
- **IWDG 升级期放宽**：`bl_wdg_ops` 增 `set_timeout_ms`；core 在 ERASE_APP 前放大到
  `BL_IWDG_UPGRADE_TIMEOUT_MS`，VERIFY 完成或跳转前恢复。F1 取同值 2000 ms（行为等价，
  路径固化）；**F4 建议 8000 ms**——128K 扇区典型擦除 ~875 ms 且单 bank 擦除期间 CPU
  停顿无法喂狗（F4 移植验收项）。
- **CMSIS 目录偏差**：原计划迁 `third_party/CMSIS/f1/`，因工具链安全钩禁止经 shell 移动
  原样拷贝的第三方源文件（字节一致性优先），维持平铺——F1/F4 文件名不冲突
  （core_cm3 vs core_cm4、stm32f10x vs stm32f4xx），家族边界由 chip.json 的文件清单与
  include 路径表达。
- 迁移顺序：F411CEU6 先行（验证 CSP 流程，硬件在位）→ F407ZGT6 复用 f4 家族层。

## 4. 默认假设确认表（../AGENTS.md §15）

| # | 假设 | 状态 | 固化位置 |
|---|---|---|---|
| 1 | USART1 PA9/PA10，115200 8N1 | 确认 | ../protocol.md §3 |
| 2 | LED PC13 低电平点亮 | 确认 | design.md ADR-008 |
| 3 | OLED SSD1306，PB9=SDA / PB8=SCL，软件 I2C，保留硬件 I2C1 重映射选项 | 确认 | ../architecture.md §3、../external_interface.md §2 |
| 4 | IWDG 约 2 s | 确认（2000 ms） | design.md ADR-011 |
| 5 | APP 基址 0x08004000 | 确认 | ../partition.md §1 |
| 6 | APP 校验 CRC32 | 固化为 ISO-HDLC | ADR-001 |
| 7 | 升级帧校验 CRC16/MODBUS | 固化 | ADR-002 |

## 5. 集中配置项（阶段 1 落地到 `board_config.h`）

以下常量全项目唯一出处为 `board_config.h`（默认值列在此处，禁止散落硬编码）：

| 常量 | 默认值 | 说明 |
|---|---|---|
| `BL_USE_HSE` | 1 | 时钟源：1=HSE 8M→PLL 72M（默认）/ 0=HSI 8MHz 回退（ADR-010） |
| `BL_BOOT_WAIT_MS` | 3000 | 启动等待窗口 |
| `BL_IWDG_TIMEOUT_MS` | 2000 | IWDG 超时 |
| `BL_UART_BAUD` | 115200 | 升级串口（USART1）波特率 |
| `BL_BT_UART_BAUD` | 115200 | 蓝牙通道（UART2/HC-05 数据模式）波特率（ADR-016） |
| `BL_PIN_LED` / `BL_PIN_I2C_SCL` / `BL_PIN_I2C_SDA` | 0 / 1 / 2 | PC13 / PB8 / PB9 |
| `BL_PIN_BT_STATE` / `BL_PIN_BT_EN` | 3 / 4 | PB0（HC-05 STATE 输入）/ PB1（HC-05 EN 输出，默认低，ADR-016） |
| `BL_APP_BASE` / `BL_APP_SIZE` | 0x08004000 / 0x0000B800 | APP 分区 |
| `BL_PARAM_BASE` / `BL_PARAM_SIZE` | 0x0800F800 / 0x00000800 | 参数区 |
| `BL_PROTOCOL_ACTIVE_MS` | 10000 | 协议活跃判定窗口 |
| `BL_UI_REFRESH_MS` | 200 | OLED 最低刷新间隔 |
| `BL_LOG_LEVEL` / `BL_LOG_DISABLE` | INFO / 0 | 日志编译期控制 |
| `BL_LOG_HEARTBEAT_MS` | 500 | 空闲心跳日志间隔（0=关闭；调试期探针值） |
