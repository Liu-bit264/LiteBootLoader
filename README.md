# LiteBootLoader — STM32F103C8T6 BootLoader 框架

可编译、可测试、可移植的 STM32 BootLoader 框架（BL）。首个目标芯片 STM32F103C8T6
（Cortex-M3，64 KiB Flash / 20 KiB RAM），通过 core/port 分层支持后续迁移到 F4 / G0 / H7。

- 项目规则与分阶段计划：见根目录 [AGENTS.md](AGENTS.md)
- 当前状态：**阶段 1 —— BL 主体已实现**（AC5 0 警告 0 错误；硬件实测：OLED 点亮、LED 状态正常、串口与升级流程待验证；代码 Review 已过一轮）

## 目录结构（阶段 0 骨架）

| 目录 | 职责 |
|---|---|
| `core/` | 状态机、协议、启动策略、元数据与通用逻辑（不依赖 HAL） |
| `port/` | 芯片端口层（`stm32f1/f103c8t6` 为首个实现，预留 f4 / g0 / h7） |
| `bsp/` | 板级器件驱动（`oled_ssd1306` 等） |
| `app/examples/` | APP 示例工程（链接到 `0x08004000`） |
| `linker/` | 链接脚本 / 分散加载文件（bootloader、app） |
| `tools/` | 上位机与工程工具：`python/` 升级器、`uvprojx/` 工程解析/生成、`ico/` 图标解析/生成、`vofa+/` 调试配置 |
| `docs/` | 设计文档：design / architecture / protocol / partition / external_interface / versioning / vofa_plus |
| `scripts/` | 工具链检查与构建脚本 |
| `third_party/` | CMSIS / HAL 依赖 |

## 快速开始

```bash
# 1) 工具链检查（Git Bash；CMD 用 scripts\check_toolchain.bat）
bash scripts/check_toolchain.sh

# 2) uvprojx 工具（Keil 工程解析 / 生成）
python tools/uvprojx/parser.py <工程.uvprojx> -o spec.json
python tools/uvprojx/generator.py spec.json -o new.uvprojx        # 创建模式（结果需在 Keil 中人工验证）
python tools/uvprojx/generator.py spec.json --update old.uvprojx  # 更新模式（保留未知字段，自动备份）

# 3) ICO 工具（图标解析 / 生成）
python tools/ico/parser.py <文件.ico>
python tools/ico/generator.py --sizes 16,32,48,256 -o icon.ico icon.png
```

## 构建（BL，阶段 1）

```bash
# 一键构建（重新生成工程 -> UV4 -r 全量重建 -> 退出码判定 -> 生成 bin）
bash scripts/build_keil.sh

# 修改 bootloader.spec.json 后重新生成 Keil 工程（自动备份旧文件）
python tools/uvprojx/generator.py bootloader.spec.json -o bootloader.uvprojx

# Keil 命令行构建（退出码：0=无警告无错误，1=有警告，≥2=有错误）
"/e/Hardware/Keil/Keil_v5/UV4/UV4.exe" -r bootloader.uvprojx -j0 -o keil_build.log

# 生成 bin（AC5 fromelf；当前 11 596 B，SHA-256 见交付记录）
"/e/Hardware/Keil/Keil_v5/ARM/ARMCC/bin/fromelf.exe" --bin --output=bootloader.bin Objects/bootloader.axf
```

> 编译器固定为 **AC5**（V5.06u7，ADR-013）；CMSIS 内核头为 V1.30（详见 `third_party/CMSIS/LICENSES.md`）。

## 工具自测（无需 pytest，直接运行）

```bash
python tools/uvprojx/test_uvprojx.py
python tools/ico/test_ico.py
```

> **环境约定**：本机 Python 解释器来源较多（Miniforge / MSYS2 / uv），默认 `python`
> 为 Miniforge base，**禁止直接装包**。任何 Python 依赖一律用 uv 隔离运行：
> `uv run --python 3.12 --with <pkg> <脚本.py>`（wheel 只进 uv 缓存，不污染环境）；
> 阶段 3 的 pyserial 即按此方式使用。详见 `AGENTS.md` 第 3 节。

## 文档（阶段 0 已交付）

| 文档 | 内容 |
|---|---|
| [docs/design.md](docs/design.md) | 总体设计与固化决策记录（CRC 参数、升级模式、LED/日志策略等 ADR） |
| [docs/architecture.md](docs/architecture.md) | 分层架构、ops 接口、运行时模型、状态机、内存预算 |
| [docs/partition.md](docs/partition.md) | Flash 分区、参数区双副本状态机与断电恢复 |
| [docs/protocol.md](docs/protocol.md) | 通信协议规范（帧格式、命令、示例帧、工具用法） |
| [docs/external_interface.md](docs/external_interface.md) | 外部接口清单（引脚、协议摘要、抽象接口、OTA 接入点） |
| [docs/versioning.md](docs/versioning.md) | SemVer 与 Conventional Commits 细则 |
| [docs/vofa_plus.md](docs/vofa_plus.md) | VOFA+ 定位与可行性说明 |

后续阶段产出：`porting_guide.md`（阶段 1）、`test_plan.md`（阶段 4）。
