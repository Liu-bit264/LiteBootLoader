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

| 芯片 | 状态 | 说明 |
|---|---|---|
| STM32F103C8T6 | 全功能 | 唯一完成 14/14 验收项、上位机升级 E2E 与断电恢复演练的支持包 |
| STM32F411CEU6 | 最小包 | 仅串口升级 + 引导跳转（无 OLED / 蓝牙 / I2C）；升级、跳转、setmeta 回环、复位注入已上板实测 |
| STM32G0 / H7 | 预留目录 | 仅骨架，可自行按移植指南补齐 |
| STM32F407ZGT6 | 规划中 | f4 家族第二个复用点 |

产物尺寸、SHA-256 与逐项验收结论见 [CHANGELOG.md](CHANGELOG.md)。

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

| 想改什么 | 改哪里 |
|---|---|
| 引脚：USART TX/RX、LED、蓝牙 STATE/EN、软件 I2C SCL/SDA | `board_config.h` 的 `BL_UART_TX_PORT`/`_NUM`、`BL_UART_RX_*`、`BL_PIN_LED*`、`BL_PIN_BT_*`、`BL_PIN_I2C_*`；并同步 `chips/<id>.json` 的 `pins` |
| 晶振与主频（如 8 MHz ↔ 25 MHz 晶振） | `board_config.h` 的 `BL_HSE_MHZ`；PLL 参数在 `port/<家族>/<型号>/clock.c` 按该宏选取 |
| 分区与擦除单元（BL / APP / 参数区） | `board_config.h` 的 `BL_FLASH_*` 与 `BL_ERASE_UNIT_TABLE`；并同步 `chips/<id>.json` 的 `partitions`/`erase_units` |
| IWDG 超时与升级期放宽值 | `board_config.h` 的 IWDG 宏（F103 2000 / 8000 ms 等） |
| 可选服务：蓝牙通道、OLED、日志级别 | `board_config.h` 的服务开关（如 `BL_TRANSPORT_BT_EN`、`BL_LOG_*`）；未启用的服务由 `core/bl_service_stub.c` 弱默认兜底，启用步骤见 [porting_guide §3.1](docs/porting_guide.md) |

改动任一侧后：`python chips/test_chip.py`（强制 chip.json ↔ board_config.h 一致 +
产物模板往返）→ `CHIP=<id> bash scripts/build_keil.sh`（重新生成产物并全量构建）。

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
