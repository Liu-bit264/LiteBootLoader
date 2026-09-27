# 更新日志（Changelog）

本项目的所有显著变更记录于此。格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [SemVer 2.0.0](https://semver.org/lang/zh-CN/)。

English version: [CHANGELOG.en.md](CHANGELOG.en.md)

## [0.1.0] - 2026-09-27

首个公开发布版本。

### Added

- **BootLoader 核心**（`core/`）：升级协议状态机与命令处理、Flash 存储与擦除单元抽象、
  参数区双副本元数据（断电安全）、启动决策、APP 合法性校验与原子跳转
- **STM32F103C8T6 支持包**（`port/stm32f1/f103c8t6/`）：flash / uart / i2c / gpio / wdg /
  clock / systick 实现，72 MHz（HSE 8 MHz + PLL，HSI 回退）
- **多芯片基础设施（CSP，ADR-015）**：`chips/<id>.json` 芯片清单（构建侧事实源）、
  spec/sct 模板化工程生成（LiteTools chipfill）、擦除单元抽象、IWDG 升级期放宽参数化
- **APP 示例**（`app/examples/f103c8t6_app/`）：呼吸灯 + 主动请求进入 BL
- **服务层**（`services/`）：OLED+LED 显示服务、USART1 日志服务（分级、编译期开关）
- **外部工具仓**：[LiteBootUpgrader](https://github.com/Liu-bit264/LiteBootUpgrader)
  （上位机 CLI + GUI）、[LiteTools](https://github.com/Liu-bit264/LiteTools)
  （Keil uvprojx / ICO 工具、chipfill CSP 填充）
- **脚本**：工具链检查（`check_toolchain.sh|.bat`）、CSP 一键构建（`build_keil.sh`）
- **文档**：用户手册、架构、协议规范（协议契约）、分区与双副本状态机、外部接口、
  移植指南、测试计划、设计决策记录（`docs/dev/`）

### Verification

- F103C8T6 硬件全链路验证：14/14 验收项通过、上位机升级 E2E、断电恢复演练
- 产物基线：BL 13 728 B（SHA-256 `ce41b1b6…`）、APP 8 072 B（SHA-256 `1ab705d6…`）

### Notes

- 0.1.0 之前的开发历史见 `git log`（该阶段未打版本 tag）
- 0.1.0 起上位机与工程工具拆分为独立仓（见上文链接），本仓不再包含其代码
