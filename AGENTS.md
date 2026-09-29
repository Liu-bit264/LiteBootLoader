# LiteBootLoader — Agent 工作规范（AGENTS.md）

> 本文件面向在本仓库工作的编码代理（Claude Code、Codex、Pi Harness 等），约束其在
> 本仓库内的工程行为；文件保存在仓库根目录，兼容 AGENTS.md 约定的代理会在会话中自动加载。
> 人类贡献者的参与流程见 [CONTRIBUTING.md](CONTRIBUTING.md)，仓库定位与现状见
> [README.md](README.md)。本文件只约束仓库内工作，不涉及代理平台的全局配置。

---

## 1. 仓库定位与当前阶段

LiteBootLoader（简称 **BL**）是一个可编译、可测试、可移植的 STM32 BootLoader 框架：
启动决策、APP 合法性校验、USART1 升级协议、Flash 与参数区管理、OLED/LED 状态显示、
IWDG 看门狗与安全跳转。

- **首个支持包**：STM32F103C8T6（已完成全链路硬件验证）；F4 / G0 / H7 端口目录预留
- **当前阶段**：开源协作——接收 Bug Issue 与芯片支持包（CSP）PR，流程见第 9 节
- **不在范围内**：联网 OTA 技术栈、外部 Flash、双 APP 分区与自动回滚（接口可预留，不实现）

输出和代码必须工程化、可编译、可验证。不要用伪代码冒充已实现代码，不要声称未实际执行的
构建或测试已经通过。

---

## 2. 代理工作纪律

### 2.1 修改原则

- 多步任务先列计划再执行，完成一项勾一项；改动保持小、可审查、可回滚。
- 修改前定位调用关系；修改后检查所有受影响接口、头文件、链接脚本和文档。
- 核心层不得依赖 STM32 HAL；芯片差异必须收敛到 `port/` 或 `bsp/`。
- 裸机实现，不使用 RTOS，不使用 `malloc`、`calloc`、`realloc` 或隐式动态分配。
- 所有外部接口、协议字段、Flash 布局和默认假设必须文档化。
- 未确认的硬件信息不得静默硬编码；必须集中在 `board_config.h` 并标注默认值。
- 优先复用已有实现，不重复创建同类模块；除非任务明确要求，不做无关的大规模重构。

### 2.2 诚实报告

- 未实际执行的构建、测试、硬件验证，明确标注「未验证」，并给出可复现命令；不得推断为通过。
- 构建结果按工具退出码判定（如 UV4：0 = 无警告无错误，1 = 有警告，≥2 = 有错误），
  不能只看是否生成了文件。
- 报告测试结果时区分：**通过 / 失败 / 未执行 / 不适用**，附证据（命令输出、SHA-256 等）。
- 查阅芯片手册（RM0008、PM0075、AN2606、CMSIS 等）与外部资料时，关键结论注明出处。

### 2.3 危险操作与环境隔离

- 删除/移动/覆盖/批量修改文件、安装卸载依赖、运行来源不明脚本、涉及密钥凭据等敏感操作，
  **建议**先放入隔离环境（容器/沙箱）执行；只读操作与常规单文件编辑可直接执行。
  是否具备隔离能力取决于代理平台自身，不具备时向用户说明再操作。
- 密钥与凭据不入源码、不入文档、不入提交；从环境变量或密钥服务读取。

---

## 3. 固定技术约束

### 3.1 参考支持包（STM32F103C8T6）

- MCU：STM32F103C8T6，Cortex-M3；Flash 64 KiB；RAM 20 KiB；Flash 页大小 1 KiB
- 时钟：优先 HSE 8 MHz + PLL = 72 MHz；HSE 不可用时回退 HSI
- 裸机，无 RTOS，无动态内存

### 3.2 外设（F103C8T6 参考配置）

- USART1：PA9/PA10，115200，8N1
- LED：PC13，低电平点亮；非阻塞状态闪烁，基础节拍 1 Hz
- OLED：0.96 英寸 SSD1306；SDA PB9 / SCL PB8；默认软件 I2C，保留硬件 I2C1 重映射选项
- IWDG：约 2 秒；BL 与 APP 均需持续喂狗；跳转 APP 前不得关闭

### 3.3 配置集中原则

引脚、时钟、分区和超时配置必须集中定义，禁止散落硬编码。双事实源（ADR-015）：

- `chips/<id>.json`：构建侧事实源（chipfill 消费，生成 spec/sct/工程）
- `port/<family>/<chip>/board_config.h`：C 侧唯一出处
- 两者由 `chips/test_chip.py` 强制一致，改动任何一侧必须跑一致性测试

---

## 4. Flash 布局与跳转（以 F103C8T6 为例）

### 4.1 默认布局

| 区域 | 起始地址 | 结束地址 | 大小 |
|---|---:|---:|---:|
| BootLoader | `0x08000000` | `0x08003FFF` | 16 KiB |
| APP | `0x08004000` | `0x0800F7FF` | 46 KiB |
| 参数区 | `0x0800F800` | `0x0800FFFF` | 2 KiB（2 页） |

要求：

- APP 链接地址为 `0x08004000`，启动后设置 `SCB->VTOR = APP_BASE`。
- 禁止 APP 升级流程擦写 BootLoader 区和参数区。
- 所有擦写操作必须检查地址范围、长度和对齐。
- 参数区采用双副本、序号和 CRC；写入流程必须具备掉电安全性。
- 参数副本状态机与断电恢复规则见 [docs/partition.md](docs/partition.md)；
  不得用含糊的「最后写有效标志」描述代替状态机。

### 4.2 跳转 APP 的最低要求

跳转前按可验证顺序完成：

1. 校验 APP 初始 MSP 位于合法 SRAM 范围。
2. 校验 APP Reset Handler 位于合法 APP Flash 范围，且 Thumb 位有效。
3. 停止新任务和非阻塞 UI 更新。
4. 关闭全局中断。
5. 停止 SysTick，并清除相关状态。
6. 反初始化 BL 使用的外设，清理待处理中断。
7. 设置 `SCB->VTOR = APP_BASE`。
8. 设置 MSP 为 APP 向量表首项。
9. 跳转到 APP Reset Handler（MSP 切换与跳转必须原子完成，不得经 C 函数中转）。

IWDG 启动后不可关闭；必须控制跳转窗口，确保 APP 能及时接管喂狗。

---

## 5. 通信协议

### 5.1 帧格式

```text
SOF(0xAA55) | VER(1B) | CMD(1B) | SEQ(1B) | LEN(LE16) |
DATA(0..256B) | CRC16(LE16) | EOF(0x55AA)
```

- CRC：CRC16/MODBUS，覆盖范围 `VER` 至 `DATA` 末尾
- `DATA` 最大 256 字节；多字节字段为小端
- 解析器必须支持字节流输入、超时复位、非法长度拒绝和帧同步恢复
- 不允许基于未对齐指针强制转换直接读取多字节字段

### 5.2 命令

`PING`、`GET_INFO`、`ERASE_APP`、`WRITE_CHUNK`（`DATA = offset(LE32) + payload`）、
`VERIFY_APP`、`SET_META`、`GET_META`、`JUMP_APP`、`RESET`。

- 响应命令为 `CMD | 0x80`，响应数据必须包含状态码。
- 最低错误码：`OK`、`CRC_ERROR`、`FLASH_ERROR`、`RANGE_ERROR`、`STATE_ERROR`、`TIMEOUT`。
- 协议规范唯一出处是 [docs/protocol.md](docs/protocol.md)（命令编号、状态码、状态机、
  重复包处理、SEQ 语义、擦除与写入先后关系、超时、重试、幂等性和异常恢复）。
- **协议变更必须先改 protocol.md 并升协议版本，再同步固件与上位机**（联动规则见 9.3）。

---

## 6. 软件架构

### 6.1 目录结构

```text
core/          状态机、协议、启动策略、元数据、CRC、版本（不依赖 HAL）
services/
  display_oled/  OLED+LED 显示服务实现
  debug_uart/    USART1 日志服务实现
chips/
  <id>.json        CSP 芯片清单（ADR-015：构建侧事实源，chipfill 消费）
  templates/       per-chip spec 模板（本项目工程结构数据）
  test_chip.py     模板往返 + chip.json↔board_config 一致性测试
port/
  bl_port.h      芯片抽象接口（ops 结构）
  stm32f1/f103c8t6/  flash/uart/i2c/gpio/wdg/clock/systick 实现
  stm32f4/ stm32g0/ stm32h7/  （预留）
bsp/           板级器件驱动（oled_ssd1306 等）
app/
  examples/f103c8t6_app/  APP 示例（链接到 0x08004000）
linker/        链接脚本 / 分散加载文件（*.sct 为 chipfill 生成产物）
tools/         vofa+/（RawData 调试帧模板；uvprojx/ico 工具在外部仓 LiteTools）
docs/
  user_manual.md       用户手册
  architecture.md      分层架构、ops 接口、内存预算
  protocol.md          通信协议规范（协议契约唯一出处）
  partition.md         Flash 分区、参数区双副本状态机
  external_interface.md 外部接口清单
  porting_guide.md     移植指南（新增芯片支持包的入口文档）
  dev/                 开发与代理工作文档（design / versioning / vofa_plus / test_plan）
scripts/       check_toolchain.*、build_keil.sh、build_keil.md（build_gcc.sh 规划中未交付）
third_party/   CMSIS / HAL 依赖（原样捆绑，许可见 third_party/CMSIS/LICENSES.md）
```

### 6.2 分层职责

- **core**：状态机、协议、启动策略、元数据和通用逻辑；不依赖 HAL
- **transport**：提供 `init/send/recv` 抽象；首个实现为 UART，接口可扩展至 CAN/SPI/I2C
- **protocol**：流式解析、组帧、命令分发、CRC、超时、重试与错误响应
- **storage**：Flash 读写、APP 擦除、APP CRC32、元数据双副本
- **boot**：启动决策、APP 合法性校验、跳转
- **ui（服务化，ADR-014）**：显示与调试为独立服务模块（`services/display_oled`、
  `services/debug_uart`），向 core 提供统一 API（`core/bl_display.h`/`core/bl_debug.h`）；
  所有周期任务必须非阻塞
- **port**：芯片与工具链相关实现（Flash、UART、I2C、GPIO、IWDG、时钟、SysTick）
- **bsp**：板级器件驱动及其配置

端口层通过明确的 ops 结构提供：`bl_flash_ops`、`bl_uart_ops`、`bl_i2c_ops`、
`bl_gpio_ops`、`bl_wdg_ops`、`bl_clock_ops`（签名见 `port/bl_port.h`）。

---

## 7. UI、日志与看门狗

### 7.1 OLED

显示：BL 版本、芯片型号、APP 状态、升级进度、CRC 状态、IWDG 状态。

显示服务为**可选插件**（ADR-019）：默认 BL 示例配置为最小集（LED 状态灯 +
串口日志，无 OLED）；OLED 能力由 `services/display_oled` 提供，启用方式见
porting_guide.md §3.1。

- 非阻塞、限频刷新；Flash 擦写和升级接收期间不得因整屏刷新造成不可接受延迟
- 提供空弱符号回调 `bl_display_user_page()`（ADR-014），供用户扩展自检页面

### 7.2 LED

PC13 低电平点亮。为至少以下状态定义互不歧义的非阻塞模式：等待升级、正在升级、
APP 无效或校验失败、即将跳转 APP、致命错误。

### 7.3 日志

- 分级：至少 ERROR、WARN、INFO、DEBUG；默认 INFO；支持编译期关闭
- 日志不得破坏升级协议；若共用 USART1，必须定义复用或隔离策略

### 7.4 IWDG

- BL 和 APP 均负责喂狗；耗时擦除、CRC 和 OLED 操作期间必须有明确喂狗点
- IWDG 升级期放宽语义（`bl_wdg_ops.set_timeout_ms`）：ERASE_APP 前放宽、
  VERIFY 完成或跳转前恢复；测试计划必须覆盖升级中断、复位和跳转窗口

---

## 8. 构建环境建议

- **主工具链**：Keil MDK（编译器固定 AC5，ADR-013；CMSIS 内核头 V1.30，
  见 `third_party/CMSIS/LICENSES.md`）；命令行构建 `UV4 -r <工程>.uvprojx -j0 -o <日志>`，
  按退出码判定结果。
- **兼容目标**：源码保持可由 `arm-none-eabi-gcc` 编译（构建脚本规划中，未交付前不作依据）。
- **工具检查**：`scripts/check_toolchain.sh|.bat` 检查 CMake、arm-none-eabi-gcc、Make/Ninja、
  OpenOCD、Python（含 pyserial）、Keil（非标准安装可用 `KEIL_UV4` 环境变量指向 UV4.exe）；
  脚本必须输出「可用 / 缺失 / 未检测」，可选工具缺失不得误报整体失败。
- **Python 依赖**：建议隔离运行（`uv run --with <pkg>` 或 venv + pip），不污染全局解释器；
  上位机/脚本对 pyserial 的依赖按此方式运行。
- **串口**：Windows 形如 `COMx`，Linux 形如 `/dev/ttyUSBx`；工具与文档不得假设固定端口。
- **换行符**：注意 git autocrlf；链接脚本、scatter 文件、`.uvprojx` 避免换行符混用。

---

## 9. 协作工作流

### 9.1 Bug 修复

1. 复现优先：能用主机侧单测复现的不依赖硬件；硬件在环复现遵循 docs/dev/test_plan.md。
2. 根因修复，最小改动；不借机重构。
3. 回归证据：构建退出码、相关测试输出；涉及时序/掉电安全的修复需硬件演练。
4. 修复涉及协议、分区、接口或默认假设时，同步对应文档与 CHANGELOG。

### 9.2 芯片支持包（CSP）变更清单

新增/修改芯片支持按以下清单执行（详细移植说明见 [docs/porting_guide.md](docs/porting_guide.md)）：

1. `chips/<id>.json`：按 `chips/f103c8t6.json` 的 schema 提供 device/build/memory/
   partitions/erase_units/clock/pins/iwdg/sysmem。
2. `port/<family>/<chip>/`：实现 `port/bl_port.h` 全部 ops；`board_config.h` 为 C 侧
   唯一常量出处，与 chip.json 保持一致。
3. 一致性：`python chips/test_chip.py` 通过（清单↔board_config 一致 + spec/sct 模板往返）。
4. 工程生成：LiteTools `chipfill.py` + `generator.py` 生成 spec/sct/uvprojx 无错。
5. 构建回归：BL/APP 编译通过，bin 尺寸不超过 chip.json 分区表限额。
6. 硬件回归（板在手时）：烧录 → 上位机升级 → 校验 → 跳转 → 断电恢复演练；
   无板时明确标注「未验证」。
7. 文档同步：architecture/partition 增补、CHANGELOG 条目、版本号按第 10 节规则。

### 9.3 三仓联动

| 仓库 | 职责 | 耦合契约 | 变更联动 |
|---|---|---|---|
| LiteBootLoader（本仓） | 固件 + 协议契约 | `docs/protocol.md` 为协议唯一规范 | 协议/参数区/跳转行为变更 → 先改文档再改码 |
| LiteBootUpgrader | 上位机 CLI/GUI | 实现本仓 protocol.md 当前版本（VER 0x01） | 本仓协议或行为变更 → LBU 同步 + `test_host_protocol.py` + 硬件 E2E |
| LiteTools | uvprojx/ico 工具 + chipfill | `chips/<id>.json` schema + spec 模板 | schema/模板变更 → LiteTools 单测 + 本仓 `chips/test_chip.py` 回归 |

### 9.4 质量基线

任何功能变更合并前，以下基线不得回退（完整对照表见 docs/dev/test_plan.md）：

1. BL 实际编译通过，`.bin` 不超过 16 KiB（按 F103C8T6 分区；其他芯片按 chip.json）。
2. APP 实际编译通过，`.bin` 不超过 46 KiB（同上）。
3. 正常升级后可跳转 APP，APP 中断正常。
4. PC13 LED 和串口日志符合状态定义。
5. OLED 显示版本、芯片、APP 状态、进度、CRC 和 IWDG 状态（适用范围：启用
   display_oled 的配置；默认 BL 示例配置为最小集——LED 状态灯 + 串口日志，
   见 design.md ADR-019）。
6. APP CRC 错误时拒绝跳转并进入升级模式。
7. 升级中断或复位后可重新升级，不误写 BL 与参数区。
8. APP 可主动请求进入 BL，且请求标志生命周期有文档与测试。
9. 参数区双副本能从写入中断中恢复，并选择最新有效副本。
10. BL 到 APP 的 IWDG 接管不会造成误复位。
11. VOFA+ 可查看日志和手动发帧；LiteBootUpgrader 可完成升级。
12. `external_interface.md` 和 `porting_guide.md` 与实现一致。
13. uvprojx 与 ICO 工具具备可运行测试。
14. 版本和建议提交信息符合 SemVer 与 Conventional Commits。

---

## 10. 版本与提交规范

- 遵循 Semantic Versioning 2.0.0；Git tag：`vX.Y.Z`
- `core/bl_version.h` 定义 `BL_VERSION_MAJOR/MINOR/PATCH/STRING`
- 遵循 Conventional Commits 1.0.0；类型：`feat`、`fix`、`docs`、`style`、`refactor`、
  `perf`、`test`、`chore`、`port`；建议 scope：`core`、`port/f1`、`port/f4`、`protocol`、
  `ui`、`bsp/oled`、`tools`、`docs`
- 版本规则：`fix` → PATCH，`feat` → MINOR，`BREAKING CHANGE` → MAJOR
- 除非维护者明确要求，代理不执行 git commit、打 tag 或推送；仅在报告中给出建议的提交信息
- 版本细则见 [docs/dev/versioning.md](docs/dev/versioning.md)

---

## 11. 外部接口文档

`docs/external_interface.md` 至少包含：

- USART1 物理参数
- 帧格式、命令表、状态码和错误码
- transport 抽象接口及 CAN/SPI/I2C 扩展方法
- storage 抽象接口
- ui 抽象接口
- 参数区布局、元数据结构、版本字段和 CRC
- APP 请求进入 BL 的机制、持久性和清除时机
- 后续 OTA 接入点；只定义接口，不实现 OTA 技术栈

---

## 12. 禁止事项

- 不假设存在外部 Flash。
- 不默认实现双 APP 或回滚。
- 不使用动态内存。
- 不允许升级路径擦写 BootLoader 自身或参数区。
- 不跳过 APP 栈顶、复位向量、地址范围和 CRC 校验。
- 不在多处硬编码引脚、时钟、分区或看门狗参数。
- 不一次性生成大量未经构建验证的代码。
- 不用阻塞式 OLED 刷新破坏升级接收和 IWDG 时序。
- 不混淆 VOFA+ 调试能力与 LiteBootUpgrader 的正式升级能力。
- 不宣称未运行的命令、硬件测试或 Keil 构建已经通过。
- 未经维护者明确授权，不执行破坏性命令、不删除文件、不覆盖现有工程配置、
  不提交或推送代码。
