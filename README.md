# LiteBootLoader — STM32 系列 BootLoader 框架

简体中文 | [English](README.en.md)

可编译、可测试、可移植的 STM32 BootLoader 框架（BL）：启动决策、APP 合法性校验、
串口升级协议与 OTA 状态查询（有线 USART1 为强制通道，蓝牙 HC-05 为可选能力）、
Flash 与参数区管理、状态显示（默认 LED 状态灯，OLED 可选）、IWDG 看门狗与安全跳转。

代码按「芯片无关 / 芯片相关」分两层：`core/` 放状态机、协议、存储与启动决策，
`port/<家族>/<型号>/` 放时钟、Flash、UART、GPIO、IWDG、SysTick 与跳转原子序列，
两层之间只经 `port/bl_port.h` 的 ops 抽象通信；`chips/<id>.json` 是构建侧事实源，
负责生成 spec / scatter / Keil 工程。加一颗芯片 = 新建 `port/` 目录 + 填 `chips/<id>.json`
+ 实现 ops（流程见 [docs/porting_guide.md](docs/porting_guide.md)）。

**板级配置的统一入口是 `board_config.h`**：引脚、晶振与主频、分区与擦除单元、IWDG 超时、
可选服务开关全部集中在这里。`port/<家族>/<型号>/board_config.h` 是 C 侧常量唯一出处，
必须与构建侧 `chips/<id>.json` 保持一致——改完任一侧跑 `python chips/test_chip.py`
强制校验（逐项对照见下文「配置」）。

## 支持的芯片

| 芯片 | 支持范围 | 验证状态 |
|---|---|---|
| STM32F103C8T6（参考支持包） | 串口升级 + 引导跳转为主干；蓝牙（UART2）与 OLED（软件 I2C）可选能力的实现均已就绪 | 唯一完成全链路硬件验证：14/14 验收项、上位机升级 E2E、断电恢复演练 |
| STM32F411CEU6（最小包） | 仅串口升级 + 引导跳转（无蓝牙 / OLED / I2C） | 编译、一致性、上板 HIL 通过：升级、跳转、setmeta 回环、复位注入实测 |
| STM32G0 / H7 | 预留端口目录（仅骨架） | 未实现 |
| STM32F407ZGT6 | 规划中：f4 家族第二个复用点 | 未开始 |

两个支持包的**默认 BL 示例配置都是最小集**（LED 状态灯 + 串口日志，ADR-019）：蓝牙与 OLED
只是可选能力，默认不启用（启用方式见下文「配置」）。F103 的 14/14 验收取自含 OLED 与蓝牙的
示例配置；各配置的产物尺寸与 SHA-256（含 0.3.0 默认最小集）见 [CHANGELOG.md](CHANGELOG.md)。

## 快速开始

完整步骤见**用户手册** [docs/user_manual.md](docs/user_manual.md)（硬件连接、首次烧录、
日常升级、指示说明、故障排查）；下面是命令速览。

```bash
# 1) 工具链检查（Git Bash / Linux；CMD 用 scripts\check_toolchain.bat）
bash scripts/check_toolchain.sh

# 2) 构建 BL + APP（chip.json + 模板 -> spec/sct -> 工程 -> UV4 全量重建 -> bin）
CHIP=f103c8t6 bash scripts/build_keil.sh

# 3) 用上位机升级 APP 并跳转（上位机在独立仓 ../LiteBootUpgrader）
uv run --python 3.12 --with pyserial ../LiteBootUpgrader/bl_upgrade.py upgrade 你的APP.bin --port COMx
uv run --python 3.12 --with pyserial ../LiteBootUpgrader/bl_upgrade.py jump --port COMx
```

BL 本体用调试器烧一次即可（用户手册 §3 给出 pyocd 逐条命令），之后升级 APP 全走串口；
图形界面双击 `../LiteBootUpgrader/bl_upgrade_gui.bat`。

> 编译器固定 **AC5**（V5.06u7，ADR-013），CMSIS 内核头 V1.30（见
> `third_party/CMSIS/LICENSES.md`）；Keil 非标准安装用 `KEIL_UV4` 环境变量指向 UV4.exe，
> 构建按退出码判定：0 = 无警告无错误、1 = 有警告、≥2 = 有错误。Python 工具建议隔离运行
> （`uv run --with <pkg>` 或 venv），避免污染全局解释器。

## 配置：board_config.h

板级配置的唯一入口是 `port/<家族>/<型号>/board_config.h`（C 侧常量唯一出处），必须与构建侧
`chips/<id>.json` 保持一致。改完任一侧：

```bash
python chips/test_chip.py              # 强制 chip.json ↔ board_config.h 一致 + 产物模板往返
CHIP=<id> bash scripts/build_keil.sh   # 重新生成产物并全量构建
```

**必选**（决定升级链路能否跑通，每块板都要按实物确认）

| 项 | 改哪里 |
|---|---|
| 串口通道引脚（唯一强制服务） | `BL_UART_TX_PORT`/`_NUM`、`BL_UART_RX_PORT`/`_NUM`（默认 USART1 PA9/PA10） |
| 晶振与主频 | F103：`BL_USE_HSE`（HSE 8 MHz ×9 = 72 MHz，频率固定）；F411：`BL_HSE_MHZ`（8 或 25 MHz）。PLL 参数在 `port/<家族>/<型号>/clock.c` |
| 分区与擦除单元（BL / APP / 参数区） | `BL_FLASH_*` / `BL_APP_*` / `BL_PARAM_*`，擦除单元宏 F103 用均匀组（`BL_ERASE_UNITS_UNIFORM` + `BL_ERASE_UNIT_BASE/_SIZE/_COUNT`）、F411 用显式表 `BL_ERASE_UNIT_TABLE`；并同步 `chips/<id>.json` 的 `partitions`/`erase_units` |
| IWDG 超时与升级期放宽值 | `BL_IWDG_TIMEOUT_MS` / `BL_IWDG_UPGRADE_TIMEOUT_MS`（F103 均 2000 ms；F411 2000 / 8000 ms） |

**可选能力**（按需启用；未启用时由 `core/bl_service_stub.c` 的弱默认兜底）

| 能力 | 启用方式 |
|---|---|
| LED 状态灯显示 | 链接 `services/display_led`（默认最小集已含）；引脚 `BL_PIN_LED*` |
| 串口日志 | 链接 `services/debug_uart`（默认最小集已含）；级别/关闭用 `BL_LOG_LEVEL_DEFAULT` / `BL_LOG_DISABLE` |
| 蓝牙 HC-05 通道（transport 通道 1） | `BL_TRANSPORT_BT_EN=1` + 蓝牙引脚 `BL_PIN_BT_STATE`/`_EN`（含 `*_PORT`/`_NUM`）+ `chips/<id>.json` 的 `build.port_files_bl` 加回 `uart2.c`；步骤见 [porting_guide §3.1](docs/porting_guide.md) |
| OLED 显示（SSD1306，软件 I2C） | 链接 `services/display_oled` + `bsp/oled_ssd1306`；引脚 `BL_PIN_I2C_SCL`/`_SDA`（含 `*_PORT`/`_NUM`）。**与默认的 `display_led` 互斥**（两者都定义 `bl_display`，须二选一） |

可选能力的引脚同样要登记进 `chips/<id>.json` 的 `pins`（`test_chip.py` 会校验两侧一致）。

## 文档

| 文档 | 内容 |
|---|---|
| [docs/user_manual.md](docs/user_manual.md) | **用户手册**：硬件连接、首次烧录、日常升级、指示说明、故障排查 |
| [docs/protocol.md](docs/protocol.md) | 通信协议规范（帧格式、命令、示例帧、工具用法）——协议契约唯一出处 |
| [docs/partition.md](docs/partition.md) | Flash 分区、参数区双副本状态机与断电恢复 |
| [docs/external_interface.md](docs/external_interface.md) | 外部接口清单（引脚、协议摘要、抽象接口、OTA 接入点） |
| [docs/architecture.md](docs/architecture.md) | 分层架构、ops 接口、运行时模型、状态机、内存预算 |
| [docs/porting_guide.md](docs/porting_guide.md) | 移植指南：新增芯片支持包、ops 实现要求、配置开关、移植陷阱 |

开发与贡献向文档（设计 ADR、版本细则、测试计划、蓝牙核实笔记、VOFA+）见
[docs/dev/](docs/dev/) 与 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 参与贡献

- Bug 反馈与芯片支持请求：GitHub Issues（`bug_report` / `chip_support` 模板）
- 贡献流程（开发环境、仓库布局、三仓联动、CSP PR 清单、质量基线）：[CONTRIBUTING.md](CONTRIBUTING.md)
- 代理/编码助手的仓库工作规范：[AGENTS.md](AGENTS.md)

## 许可

本项目原创代码（`core/`、`port/`、`services/`、`bsp/`、`app/`、`linker/`、`tools/`、
`scripts/`、`docs/`）以 [MIT](LICENSE) 许可证发布（© 2026 Qingc）。

`third_party/` 下捆绑的第三方文件为**原样拷贝**（未做任何修改），保留其原始许可与版权
声明；来源、许可条款与逐字节核验记录见 [third_party/CMSIS/LICENSES.md](third_party/CMSIS/LICENSES.md)。
