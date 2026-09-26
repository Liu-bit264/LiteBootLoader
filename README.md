# LiteBootLoader — STM32F103C8T6 BootLoader 框架

可编译、可测试、可移植的 STM32 BootLoader 框架（BL）。首个目标芯片 STM32F103C8T6
（Cortex-M3，64 KiB Flash / 20 KiB RAM），通过 core/port 分层支持后续迁移到 F4 / G0 / H7。

- 项目规则与分阶段计划：见根目录 [AGENTS.md](AGENTS.md)
- 当前状态：**阶段 4 —— 14/14 验收项通过（对照表见 docs/test_plan.md）；CSP 多芯片基础设施已落地（ADR-015），BL 13 540 B**；上位机工具已迁至独立仓 LiteBootUpgrader（v1.1.2：自动"请求回 BL"、重试层、15/15 selftest、升级中断恢复演练，均在硬件通过）
- **用户手册（怎么用看这里）**：[docs/user_manual.md](docs/user_manual.md)

## 目录结构（阶段 0 骨架）

| 目录 | 职责 |
|---|---|
| `core/` | 状态机、协议、启动策略、元数据与通用逻辑（不依赖 HAL） |
| `port/` | 芯片端口层（`stm32f1/f103c8t6` 为首个实现，预留 f4 / g0 / h7） |
| `bsp/` | 板级器件驱动（`oled_ssd1306` 等） |
| `app/examples/` | APP 示例工程（链接到 `0x08004000`） |
| `linker/` | 链接脚本 / 分散加载文件（bootloader、app） |
| `tools/` | 工程工具：`uvprojx/` 工程解析/生成、`ico/` 图标解析/生成、`vofa+/` 调试帧模板（上位机已迁出：独立仓 `../LiteBootUpgrader`） |
| `docs/` | 设计文档：design / architecture / protocol / partition / external_interface / versioning / vofa_plus |
| `scripts/` | 工具链检查与构建脚本 |
| `third_party/` | CMSIS / HAL 依赖 |

## 快速开始

```bash
# 1) 工具链检查（Git Bash；CMD 用 scripts\check_toolchain.bat）
bash scripts/check_toolchain.sh

# 2) uvprojx 工具（独立仓 ../LiteTools；Keil 工程解析 / 生成）
uv run --python 3.12 ../LiteTools/uvprojx/parser.py <工程.uvprojx> -o spec.json
uv run --python 3.12 ../LiteTools/uvprojx/generator.py spec.json -o new.uvprojx        # 创建模式（结果需在 Keil 中人工验证）
uv run --python 3.12 ../LiteTools/uvprojx/generator.py spec.json --update old.uvprojx  # 更新模式（保留未知字段，自动备份）

# 3) ICO 工具（独立仓 ../LiteTools；图标解析 / 生成）
uv run --python 3.12 ../LiteTools/ico/parser.py <文件.ico>
uv run --python 3.12 --with pillow ../LiteTools/ico/generator.py --sizes 16,32,48,256 -o icon.ico icon.png
```

## 构建（CSP，ADR-015）

```bash
# 一键构建（chip.json+模板 -> spec/sct -> 生成工程 -> UV4 -r 全量重建 -> 退出码判定 -> 生成 bin）
# CHIP 指定芯片清单（默认 f103c8t6）；TARGETS 默认 "bootloader app"
CHIP=f103c8t6 bash scripts/build_keil.sh

# 手工等价流程（uvprojx 工具在独立仓 ../LiteTools）：
python ../LiteTools/uvprojx/chipfill.py --chip chips/f103c8t6.json --target bootloader \
    --spec-out bootloader.spec.json --sct-out linker/bootloader.sct   # 芯片清单 -> spec 与 scatter
python ../LiteTools/uvprojx/generator.py bootloader.spec.json -o bootloader.uvprojx  # spec -> 工程
"/e/Hardware/Keil/Keil_v5/UV4/UV4.exe" -r bootloader.uvprojx -j0 -o keil_build.log
# 退出码：0=无警告无错误，1=有警告，≥2=有错误
"/e/Hardware/Keil/Keil_v5/ARM/ARMCC/bin/fromelf.exe" --bin --output=bootloader.bin Objects/bootloader.axf
# BL 当前 13 540 B @72MHz（ADR-015 擦除单元抽象 + IWDG 放宽后），产物 SHA-256 见交付记录

# 改了 chips/<id>.json 或模板后：重新生成全部产物（test_chip.py 会强制往返一致）
python chips/test_chip.py

# 经 BL 协议升级 APP 并跳转（上位机在独立仓 ../LiteBootUpgrader，用法见 docs/protocol.md；依赖隔离见下）
uv run --python 3.12 --with pyserial ../LiteBootUpgrader/bl_upgrade.py upgrade app/examples/f103c8t6_app/app.bin --port COM4
uv run --python 3.12 --with pyserial ../LiteBootUpgrader/bl_upgrade.py jump --port COM4
```

> 编译器固定为 **AC5**（V5.06u7，ADR-013）；CMSIS 内核头为 V1.30（详见 `third_party/CMSIS/LICENSES.md`）。

## 工具自测（无需 pytest，直接运行）

```bash
# uvprojx / ico 工具单测（工具在独立仓 ../LiteTools）
cd ../LiteTools/uvprojx && python test_uvprojx.py && cd ../../LiteBootLoader
cd ../LiteTools/ico && python test_ico.py && cd ../../LiteBootLoader

# 主仓 CSP 一致性（chip.json ↔ board_config.h + 模板往返）
python chips/test_chip.py
```

> **环境约定**：本机 Python 解释器来源较多（Miniforge / MSYS2 / uv），默认 `python`
> 为 Miniforge base，**禁止直接装包**。任何 Python 依赖一律用 uv 隔离运行：
> `uv run --python 3.12 --with <pkg> <脚本.py>`（wheel 只进 uv 缓存，不污染环境）；
> 阶段 3 的 pyserial 即按此方式使用。详见 `AGENTS.md` 第 3 节。

## 文档（阶段 0 已交付）

| 文档 | 内容 |
|---|---|
| [docs/user_manual.md](docs/user_manual.md) | **用户手册**：硬件连接、首次烧录、日常升级、指示说明、故障排查 |
| [docs/design.md](docs/design.md) | 总体设计与固化决策记录（CRC 参数、升级模式、LED/日志策略等 ADR） |
| [docs/architecture.md](docs/architecture.md) | 分层架构、ops 接口、运行时模型、状态机、内存预算 |
| [docs/partition.md](docs/partition.md) | Flash 分区、参数区双副本状态机与断电恢复 |
| [docs/protocol.md](docs/protocol.md) | 通信协议规范（帧格式、命令、示例帧、工具用法） |
| [docs/external_interface.md](docs/external_interface.md) | 外部接口清单（引脚、协议摘要、抽象接口、OTA 接入点） |
| [docs/versioning.md](docs/versioning.md) | SemVer 与 Conventional Commits 细则 |
| [docs/vofa_plus.md](docs/vofa_plus.md) | VOFA+ 定位与可行性说明 |
| [docs/porting_guide.md](docs/porting_guide.md) | 移植指南：ops 实现要求、时钟双路径、跳转原子性、移植陷阱 |
| [docs/test_plan.md](docs/test_plan.md) | 测试计划：四级测试、selftest 清单、§13 验收对照表、实测教训索引 |

## 许可

本项目原创代码（`core/`、`port/`、`services/`、`bsp/`、`app/`、`linker/`、`tools/`、`scripts/`、`docs/`）以 [MIT](LICENSE) 许可证发布（© 2026 Qingc）。

`third_party/` 下捆绑的第三方文件为**原样拷贝**（未做任何修改），保留其原始许可与版权声明；来源、许可条款与逐字节核验记录见 [third_party/CMSIS/LICENSES.md](third_party/CMSIS/LICENSES.md)。
