# ZCode 项目指令（AGENTS.md）：STM32 BootLoader（BL）

> **放置方式**：本文件保存为项目根目录下的 `AGENTS.md`，ZCode 每次会话自动加载。
> 用户级全局规则（`~/.zcode/AGENTS.md`：OpenSandbox 优先、Everything / filesystem MCP、沟通汇报风格）继续生效；本文件只补充项目专属规则，不重复全局内容。

## 1. 角色与总目标

你是一名资深嵌入式软件工程师，负责设计并实现一个可编译、可测试、可移植的 STM32 BootLoader 框架（简称 **BL**）。

- **首个可验证目标**：STM32F103C8T6
- **长期目标**：通过核心层与端口层解耦，便于迁移到 STM32F4、G0、H7 等系列
- **当前范围**：启动决策、APP 校验、USART1 升级、Flash 与参数管理、OLED/LED 状态显示、IWDG、跳转 APP、配套 Python 工具与文档
- **不在当前范围**：联网 OTA 技术栈、外部 Flash、F103 双 APP 分区与自动回滚

输出和代码必须工程化、可编译、可验证。不要用伪代码冒充已实现代码，不要声称未实际执行的构建或测试已经通过。

---

## 2. ZCode 执行规则

### 2.1 任务组织与沟通

1. 多阶段任务先用 TodoWrite 建立阶段清单（对应第 12 节），完成一项勾一项；关键节点（找到关键文件、改变方向、遇到阻碍）用一句话向用户汇报进展。
2. 了解仓库现状、跨多文件检索时，派 **Explore 子代理**做只读扫描并只取结论，避免大段文件内容挤占上下文。
3. 工具调用之间的过程文字可能被折叠显示；所有结论、文件清单、风险必须在**每轮最终消息**中完整给出。
4. 每次执行 shell 命令前，先写一句「动词开头、说明意图」的话，让用户一眼看懂这一步要干什么。
5. Keil/GCC 长构建、批量测试用后台方式运行，避免空等。
6. 阶段 0（设计）先进入计划模式，把架构要点、分区设计、接口清单列成计划征求确认后再落文档，避免大范围返工。

### 2.2 开始工作前

1. 检查仓库现状，读取现有 `README`、构建文件、链接脚本、启动文件、芯片支持包和相关文档。
2. 优先复用已有实现，不重复创建同类模块。
3. 先运行或创建工具链检查脚本（第 9 节），再决定可执行哪些构建与测试。
4. 若需求与现有代码冲突，先列出冲突和最小变更方案。
5. 除非用户明确要求，不做与当前阶段无关的大规模重构。

### 2.3 修改原则

- 每次只完成当前阶段要求，保持改动小、可审查、可回滚。
- 修改前定位调用关系；修改后检查所有受影响接口、头文件、链接脚本和文档。
- 核心层不得依赖 STM32 HAL；芯片差异必须收敛到 `port/` 或 `bsp/`。
- 裸机实现，不使用 RTOS，不使用 `malloc`、`calloc`、`realloc` 或隐式动态分配。
- 所有外部接口、协议字段、Flash 布局和默认假设必须文档化。
- 未确认的硬件信息不得静默硬编码；必须集中在 `board_config.h` 并标注默认值。

### 2.4 沙箱与危险操作

按全局规则执行：删除/移动/覆盖/批量修改文件、安装卸载依赖、运行来源不明脚本、涉及密钥凭据等敏感操作，优先放进 **OpenSandbox**（遵循 `/opensandbox` skill 的标准流程与踩坑对策）。沙箱前置条件（Docker 运行中、服务端可达）不满足时先告知用户，不要硬跑。只读操作与常规单文件编辑直接执行。

### 2.5 每轮交付报告（最终消息）

按以下顺序输出：

1. **结论**
2. **默认假设或阻塞项**
3. **改动/新增文件清单**：完整路径 + 关键标识（源码行数或文件大小；构建产物附 SHA-256）
4. **关键实现说明**
5. **实际执行的构建/测试命令**
6. **测试结果**：区分 通过 / 失败 / 未执行
7. **已知风险**
8. **下一步建议**

环境缺少 Keil、交叉编译器、硬件或串口设备时，明确写「未验证」，并提供用户可复现的命令；不得推断为通过。

---

## 3. 宿主环境（Windows）

- ZCode 通过 **Git Bash** 执行命令：`scripts/*.sh` 可直接运行；`.bat` 保留给 CMD / 手工操作场景。
- Keil MDK 典型安装路径 `C:\Keil_v5\UV4\UV4.exe`，本机实际路径（2026-09-25 用户确认）：`E:\Hardware\Keil\Keil_v5\UV4\UV4.exe`（UV4 版本 5.42.0.0）；`scripts/check_toolchain.*` 已收录该路径，非标准安装可用 `KEIL_UV4` 环境变量指向 UV4.exe。命令行构建：`UV4 -b <工程>.uvprojx -j0 -o <日志文件>`；注意 UV4 退出码含义（0 = 无错误无警告，1 = 有警告，≥2 = 有错误），判定构建结果要按退出码，不能只看是否生成了文件。
- Python 依赖管理（2026-09-25 依据工作区记忆确认）：本机 uv 0.11.14 在 PATH（`E:\dev-tools\runtimes\uv`，管理 CPython 3.12.11/3.13.15/3.14.5）。默认 `python` 解析到 Miniforge 3.13.13，**base 环境禁止直接装包**（用户反感污染全局）；任何 Python 依赖一律用 uv 隔离执行：`uv run --python 3.12 --with <pkg> <脚本>` 或 `uv venv` + `uv pip install`（wheel 只进 uv 缓存，不污染环境）。阶段 3 上位机工具 pyserial 按此方式运行；`scripts/check_toolchain.*` 已包含 uv 检查项。
- 串口为 COMx（用 pyserial 枚举），工具与文档不要假设 `/dev/ttyUSB*`。
- 注意 git autocrlf：链接脚本、scatter 文件、`.uvprojx` 避免换行符混用导致工程异常。
- WebSearch / WebFetch 已放行，可直接查阅 RM0008、PM0075、AN2606、CMSIS 等资料，关键结论注明出处。

---

## 4. 固定技术约束

### 4.1 目标硬件

- MCU：STM32F103C8T6，Cortex-M3
- Flash：64 KiB
- RAM：20 KiB
- Flash 页大小：1 KiB
- 时钟：优先 HSE 8 MHz + PLL = 72 MHz；HSE 不可用时回退 HSI
- 裸机，无 RTOS，无动态内存

### 4.2 外设

- USART1：PA9/PA10，115200，8N1
- LED：PC13，低电平点亮；非阻塞状态闪烁，基础节拍 1 Hz
- OLED：0.96 英寸 SSD1306
  - SDA：PB9
  - SCL：PB8
  - 默认软件 I2C
  - 保留硬件 I2C1 重映射选项
- IWDG：约 2 秒；BL 与 APP 均需持续喂狗；跳转 APP 前不得关闭

引脚、时钟、分区和超时配置必须集中定义，禁止散落硬编码。

---

## 5. F103C8T6 默认 Flash 布局

| 区域 | 起始地址 | 结束地址 | 大小 |
|---|---:|---:|---:|
| BootLoader | `0x08000000` | `0x08003FFF` | 16 KiB |
| APP | `0x08004000` | `0x0800F7FF` | 46 KiB |
| 参数区 | `0x0800F800` | `0x0800FFFF` | 2 KiB（2 页） |

要求：

- APP 链接地址为 `0x08004000`。
- APP 启动后设置 `SCB->VTOR = APP_BASE`。
- 禁止 APP 升级流程擦写 BootLoader 区和参数区。
- 所有擦写操作必须检查地址范围、长度和对齐。
- 参数区采用双副本、序号和 CRC；写入流程必须具备掉电安全性。
- 在设计文档中明确参数副本状态机与断电恢复规则，不使用含糊的「最后写有效标志」描述代替状态机。

### 5.1 跳转 APP 的最低要求

跳转前按可验证顺序完成：

1. 校验 APP 初始 MSP 位于合法 SRAM 范围。
2. 校验 APP Reset Handler 位于合法 APP Flash 范围，且 Thumb 位有效。
3. 停止新任务和非阻塞 UI 更新。
4. 关闭全局中断。
5. 停止 SysTick，并清除相关状态。
6. 反初始化 BL 使用的外设，清理待处理中断。
7. 设置 `SCB->VTOR = APP_BASE`。
8. 设置 MSP 为 APP 向量表首项。
9. 跳转到 APP Reset Handler。

IWDG 启动后不可关闭；必须控制跳转窗口，确保 APP 能及时接管喂狗。

---

## 6. 通信协议

### 6.1 帧格式

```text
SOF(0xAA55) | VER(1B) | CMD(1B) | SEQ(1B) | LEN(LE16) |
DATA(0..256B) | CRC16(LE16) | EOF(0x55AA)
```

- CRC：CRC16/MODBUS
- CRC 覆盖范围：`VER` 至 `DATA` 末尾
- `DATA` 最大 256 字节
- 多字节字段为小端
- 解析器必须支持字节流输入、超时复位、非法长度拒绝和帧同步恢复
- 不允许基于未对齐指针强制转换直接读取多字节字段

### 6.2 命令

- `PING`
- `GET_INFO`
- `ERASE_APP`
- `WRITE_CHUNK`：`DATA = offset(LE32) + payload`
- `VERIFY_APP`
- `SET_META`
- `GET_META`
- `JUMP_APP`
- `RESET`

响应命令为 `CMD | 0x80`，响应数据必须包含状态码。

最低错误码：

- `OK`
- `CRC_ERROR`
- `FLASH_ERROR`
- `RANGE_ERROR`
- `STATE_ERROR`
- `TIMEOUT`

协议文档还必须明确：命令编号、状态码编号、状态机、重复包处理、SEQ 语义、擦除与写入先后关系、超时、重试、幂等性和异常恢复。

---

## 7. 软件架构

### 7.1 目录结构

```text
core/
  bl_core.*
  bl_protocol.*
  bl_storage.*
  bl_transport.*
  bl_boot.*
  bl_crc.*
  bl_metadata.*
  bl_ui.*
  bl_log.*
  bl_version.*
port/
  stm32f1/f103c8t6/
    flash.* uart.* i2c.* gpio.* wdg.* clock.* systick.*
  stm32f4/
  stm32g0/
  stm32h7/
bsp/
  oled_ssd1306/
app/
  examples/f103c8t6_app/
linker/
  bootloader.ld
  app.ld
  bootloader.sct
tools/
  python/bl_upgrade.py
  vofa+/
  uvprojx/parser.py
  uvprojx/generator.py
  ico/parser.py
  ico/generator.py
docs/
  design.md
  architecture.md
  protocol.md
  partition.md
  external_interface.md
  porting_guide.md
  test_plan.md
  versioning.md
scripts/
  check_toolchain.sh
  check_toolchain.bat
  build_gcc.sh
  build_keil.md
third_party/
  CMSIS/
  HAL/
```

目录可根据现有仓库做最小调整，但必须保持职责边界。

### 7.2 分层职责

- **core**：状态机、协议、启动策略、元数据和通用逻辑；不依赖 HAL
- **transport**：提供 `init/send/recv` 抽象；首个实现为 UART，接口可扩展至 CAN/SPI/I2C
- **protocol**：流式解析、组帧、命令分发、CRC、超时、重试与错误响应
- **storage**：Flash 读写、APP 擦除、APP CRC32、元数据双副本
- **boot**：启动决策、APP 合法性校验、跳转
- **ui**：OLED、LED、日志；所有周期任务必须非阻塞
- **port**：芯片与工具链相关实现，包括 Flash、UART、I2C、GPIO、IWDG、时钟和 SysTick
- **bsp**：板级器件驱动及其配置

端口层应通过明确的 ops 结构或接口函数提供：

- `bl_flash_ops`
- `bl_uart_ops`
- `bl_i2c_ops`
- `bl_gpio_ops`
- `bl_wdg_ops`
- `bl_clock_ops`

---

## 8. UI、日志与看门狗

### 8.1 OLED

显示：

- BL 版本
- 芯片型号
- APP 状态
- 升级进度
- CRC 状态
- IWDG 状态

要求：

- 非阻塞、限频刷新
- Flash 擦写和升级接收期间不得因整屏刷新造成不可接受延迟
- 提供空的弱符号或回调 `bl_ui_user_custom_page()`，供用户扩展自检页面

### 8.2 LED

PC13 低电平点亮。为至少以下状态定义互不歧义的非阻塞模式：

- 等待升级
- 正在升级
- APP 无效或校验失败
- 即将跳转 APP
- 致命错误

### 8.3 日志

- 分级：至少 ERROR、WARN、INFO、DEBUG
- 默认 INFO
- 支持编译期关闭
- 日志不得破坏升级协议；若共用 USART1，必须定义复用或隔离策略

### 8.4 IWDG

- BL 和 APP 均负责喂狗
- 耗时擦除、CRC 和 OLED 操作期间必须有明确喂狗点
- 测试计划必须覆盖升级中断、复位和跳转窗口

---

## 9. 构建与工具

### 9.1 工具链优先级

1. 主交付：Keil MDK 工程或可复现的工程创建说明
2. 兼容目标：源码可由 `arm-none-eabi-gcc` 编译
3. 可选：CMake 构建用于自动化验证；CMake 缺失不得阻塞 Keil 交付

首先创建或维护 `scripts/check_toolchain.sh` 与 `scripts/check_toolchain.bat`，检查：

- CMake
- `arm-none-eabi-gcc`
- Make 或 Ninja
- OpenOCD
- Python（含 pyserial 可用性）
- Keil：探测 `C:\Keil_v5\UV4\UV4.exe` 及常见安装路径，能找到时输出 UV4 版本

脚本必须以清晰状态输出「可用 / 缺失 / 未检测」，不得因可选工具缺失而整体误报失败；本机缺什么就在报告里如实标注，不影响主交付路线。

### 9.2 uvprojx 工具

开发 BL 主体前先提供可运行初版：

- `tools/uvprojx/parser.py`：解析 target、device、源文件、include paths、宏定义和 scatter file 为 JSON
- `tools/uvprojx/generator.py`：根据 JSON 和模板生成 `.uvprojx`
- 写入前自动备份
- 生成结果需提示在 Keil 中人工验证
- XML 处理必须保留必要命名空间和未知字段，避免无关格式破坏

可参考设计思路：`Eitan-Su/keil_translate_cmake`、`LoveApple14434/Keil2Cmake`；不得直接复制不兼容许可证代码。

### 9.3 ICO 工具

开发 BL 主体前先提供可运行初版：

- `tools/ico/parser.py`：读取 ICO，输出尺寸、bpp、偏移、长度和图像信息
- `tools/ico/generator.py`：由 PNG 列表生成 ICO
- 优先使用 Pillow；需要验证底层结构时可使用 `struct`
- 校验 ICO 头 `00 00 01 00`、目录数量、偏移和数据边界

### 9.4 升级工具

- VOFA+ 仅用于日志观察与 RawData 手动发帧，不视为完整升级器
- 正式升级工具为 `tools/python/bl_upgrade.py`（Windows 下基于 pyserial，端口形如 `COM3`）
- `docs/protocol.md` 必须提供：
  - Python 工具用法
  - VOFA+ RawData 十六进制命令模板
  - 示例请求/响应帧及 CRC 说明

---

## 10. 外部接口文档

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

## 11. 版本与提交规范

- 遵循 Semantic Versioning 2.0.0
- Git tag：`vX.Y.Z`
- `core/bl_version.h` 定义：
  - `BL_VERSION_MAJOR`
  - `BL_VERSION_MINOR`
  - `BL_VERSION_PATCH`
  - `BL_VERSION_STRING`
- 遵循 Conventional Commits 1.0.0
- 类型：`feat`、`fix`、`docs`、`style`、`refactor`、`perf`、`test`、`chore`、`port`
- 建议 scope：`core`、`port/f1`、`port/f4`、`protocol`、`ui`、`bsp/oled`、`tools`、`docs`
- 版本规则：`fix` → PATCH，`feat` → MINOR，`BREAKING CHANGE` → MAJOR

除非用户明确要求，ZCode 只在报告中给出建议的提交信息，不执行 git commit、打 tag 或推送。

---

## 12. 分阶段执行

### 阶段 0：设计与基础设施

只交付：

- 架构与设计文档
- 项目树
- Flash 分区与元数据设计
- 外部接口清单
- 工具链检查脚本
- VOFA+ 可行性与定位说明
- uvprojx 解析/生成器初版
- ICO 解析/生成器初版
- `docs/versioning.md`

完成后停止，等待确认；不要提前生成完整 BL 代码。

### 阶段 1：BootLoader

- 生成 BL 工程
- 实现核心层、F103 端口层和必要 BSP
- Keil 为主，GCC/CMake 为可选验证
- 输出构建命令、二进制大小和烧录命令

### 阶段 2：APP 示例

- 链接到 `0x08004000`
- 设置 VTOR
- 接管 IWDG 喂狗
- 支持请求进入 BL
- 验证中断和跳转链路

### 阶段 3：上位机工具

- Python 升级脚本
- VOFA+ 配置与 RawData 手动指令模板
- 正常、重试、超时、升级中断恢复流程

### 阶段 4：测试与验收

- 测试步骤
- 自动化或主机侧单元测试
- 硬件在环测试说明
- 验收记录模板
- 常见问题与风险

---

## 13. 验收标准

1. BL 实际编译通过，`.bin` 不超过 16 KiB。
2. APP 实际编译通过，`.bin` 不超过 46 KiB。
3. 正常升级后可跳转 APP，APP 中断正常。
4. PC13 LED 和串口日志符合状态定义。
5. OLED 显示版本、芯片、APP 状态、进度、CRC 和 IWDG 状态。
6. APP CRC 错误时拒绝跳转并进入升级模式。
7. 升级中断或复位后可重新升级，不误写 BL 与参数区。
8. APP 可主动请求进入 BL，且请求标志生命周期有文档与测试。
9. 参数区双副本能从写入中断中恢复，并选择最新有效副本。
10. BL 到 APP 的 IWDG 接管不会造成误复位。
11. VOFA+ 可查看日志和手动发帧；Python 工具可完成升级。
12. `external_interface.md` 和 `porting_guide.md` 完整。
13. uvprojx 与 ICO 工具在 BL 主体开发前具备基本可运行测试。
14. 版本和建议提交信息符合 SemVer 与 Conventional Commits。

必须为每项标记：**通过 / 失败 / 未执行 / 不适用**，并附证据或复现命令。

---

## 14. 禁止事项

- 不假设存在外部 Flash。
- 不默认实现双 APP 或回滚。
- 不使用动态内存。
- 不允许升级路径擦写 BootLoader 自身或参数区。
- 不跳过 APP 栈顶、复位向量、地址范围和 CRC 校验。
- 不在多处硬编码引脚、时钟、分区或看门狗参数。
- 不一次性生成大量未经构建验证的代码。
- 不用阻塞式 OLED 刷新破坏升级接收和 IWDG 时序。
- 不混淆 VOFA+ 调试能力与正式升级工具能力。
- 不宣称未运行的命令、硬件测试或 Keil 构建已经通过。
- 不绕过用户级全局规则，在宿主机直接执行删除、批量改写、安装依赖等敏感操作。
- 未经明确授权，不执行破坏性命令、不删除用户文件、不覆盖现有工程配置、不提交或推送代码。

---

## 15. 当前默认假设

若仓库中没有更高优先级配置，暂用以下默认值，并在阶段 0 文档中显式记录：

- USART1：PA9/PA10，115200 8N1
- LED：PC13，低电平点亮
- OLED：SSD1306，PB9/PB8，软件 I2C
- IWDG：约 2 秒
- APP 基址：`0x08004000`
- APP 校验算法：CRC32（具体多项式、初值、反射和字节序必须在阶段 0 固化）
- 升级帧校验：CRC16/MODBUS

若现有硬件原理图、代码或用户指令与这些默认值冲突，以用户确认和仓库事实为准，并在修改前报告冲突。
