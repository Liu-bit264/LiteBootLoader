# 贡献指南（CONTRIBUTING）

感谢关注 LiteBootLoader！本页说明如何反馈问题与参与贡献。固件的工作方式见
[docs/user_manual.md](docs/user_manual.md)，仓库现状见 [README.md](README.md)。

## 反馈 Bug

提交 Issue 时请使用 **Bug 报告** 模板，尽量附上：

1. BL/APP 版本（上位机 `info` 命令返回值，或 `core/bl_version.h`）
2. 芯片与硬件环境（板型、调试器、串口工具）
3. 复现步骤（精确到命令与时序）
4. 串口日志或协议帧记录（十六进制，可用 VOFA+ / 上位机 `listen`/`raw` 抓取）
5. 期望行为与实际行为

升级链路问题请说明阶段：擦除 / 写入 / 校验 / 跳转 / APP 运行，以及是否涉及断电或复位。

## 请求芯片支持

使用 **芯片支持请求** 模板，说明：目标芯片型号、Flash/RAM 与页（扇区）结构、
Keil DFP 包版本、时钟方案、硬件是否在手。有板子的请求可优先获得验证支持。

也可以直接按下方「芯片支持包 PR」清单自行提交移植。

## 贡献代码

### 开发环境

- 运行 `scripts/check_toolchain.sh`（Git Bash/Linux）或 `scripts/check_toolchain.bat`（CMD）
  检查 CMake、arm-none-eabi-gcc、Make/Ninja、OpenOCD、Python、Keil（UV4）
- Python 依赖建议隔离运行：`uv run --with <pkg>` 或 venv；避免污染全局解释器
- 主工具链为 Keil MDK（编译器 AC5）；注意 Windows 下 git autocrlf，勿让
  `.sct`/`.uvprojx` 混入异常换行符

### Bug 修复 PR

1. 复现优先：能写主机侧单测的先写单测；硬件在环用例参考 docs/dev/test_plan.md
2. 根因修复，最小改动；不借机重构
3. 附回归证据：构建退出码、测试输出；涉及时序/掉电安全的修复需硬件演练说明
4. 涉及协议、分区、接口或默认假设时，同步对应文档与 CHANGELOG

### 芯片支持包（CSP）PR

按以下清单执行（详细移植说明见 [docs/porting_guide.md](docs/porting_guide.md)）：

1. `chips/<id>.json`：按 `chips/f103c8t6.json` 的 schema 提供 device / build / memory /
   partitions / erase_units / clock / pins / iwdg / sysmem
2. `port/<family>/<chip>/`：实现 `port/bl_port.h` 全部 ops（flash / uart / i2c / gpio /
   wdg / clock / systick）；`board_config.h` 为 C 侧唯一常量出处，与 chip.json 一致
3. 一致性：`python chips/test_chip.py` 通过（清单↔board_config 一致 + spec/sct 模板往返）
4. 工程生成：LiteTools `chipfill.py` + `generator.py` 生成 spec/sct/uvprojx 无错
5. 构建回归：BL/APP 编译通过，bin 尺寸不超过 chip.json 分区表限额
6. 硬件回归（板在手时）：烧录 → 上位机升级 → 校验 → 跳转 → 断电恢复演练；
   无板时在 PR 中明确标注「未验证」
7. 文档同步：architecture / partition 增补、CHANGELOG 条目

### 质量基线

合并前以下基线不得回退（完整清单见 AGENTS.md §9.4 与 docs/dev/test_plan.md）：
BL/APP 编译通过且尺寸达标、升级-校验-跳转链路完好、参数区断电恢复、
APP CRC 错误拒绝跳转、IWDG 接管无误复位。

## 提交规范

- 提交信息遵循 **Conventional Commits 1.0.0**：`feat` / `fix` / `docs` / `style` /
  `refactor` / `perf` / `test` / `chore` / `port`（细则见 docs/dev/versioning.md）
- 版本遵循 **SemVer 2.0.0**：`fix` → PATCH，`feat` → MINOR，`BREAKING CHANGE` → MAJOR
- 一个 PR 聚焦一件事；大改动建议先开 Issue 讨论
