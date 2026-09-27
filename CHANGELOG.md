# 更新日志（Changelog）

本项目的所有显著变更记录于此。格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [SemVer 2.0.0](https://semver.org/lang/zh-CN/)。

English version: [CHANGELOG.en.md](CHANGELOG.en.md)

## [0.2.0] - 2026-09-27

规划书《空口蓝牙串口及OTA》落地（ADR-016）：蓝牙空口升级 + WIFI API 预留 + OTA 查询。

### Added

- **蓝牙 transport 通道**（目标 1/2）：HC-05（BT 2.0 SPP）经 USART2（PA2/PA3）接入为
  transport 通道 1（`port/stm32f1/f103c8t6/uart2.c`），STATE→PB0 / EN→PB1 经
  `bl_gpio_ops`；数据模式一次性 AT 配置 115200（`BL_BT_UART_BAUD`），联网核实结论见
  [docs/dev/bluetooth_notes.md](docs/dev/bluetooth_notes.md)
- **transport 多通道化**：`core/bl_transport.c` 重写为通道注册表 + 活动通道仲裁
  （静默 2 s 释放，与帧内字节超时同窗），protocol 层无感切换；WIFI 为同签名占位 stub
  （`port/wifi_stub.c`，目标 2 只预留 API，实接入时补实现）；顺带修正 transport 直连
  芯片端口头的分层破绽（统计改经 bl_port.h 声明）
- **OTA 命令 0x10 OTA_QUERY**（目标 3，轻量方案，用户确认）：BL/APP 版本、APP 实时
  有效性、元数据 seq、请求到达通道、蓝牙连接状态一次查询；升级复用既有幂等命令；
  0x11–0x1F 继续预留
- OLED 状态行新增蓝牙连接指示（BT:OK/BT:--）；诊断 RX 计数改为各通道累计

### Changed

- BL 版本 0.1.0 → 0.2.0；协议 0x10–0x1F 预留区间细分（0x10 已实现）
- 文档同步：architecture §6.1（通道注册表与仲裁）、protocol §3/§4.3/§5.10/§7.5、
  external_interface §1.1/§1.4/§3.1/§6、design ADR-016、porting_guide §3.1、
  partition §1（OTA 边界与通道无关）、user_manual 蓝牙章节
- 上位机配套：LiteBootUpgrader v1.3.0（`ota` 子命令、`--conn bt`、GUI 连接类型/OTA 查询）

### Verification

- AC5 全量重建 0 错 0 警：BL 15 324 B ≤ 16 KiB（SHA-256 `499de4bc…`）、
  APP 8 152 B ≤ 46 KiB（SHA-256 `534eb656…`）；`chips/test_chip.py` 11/11 通过
- 蓝牙硬件在环（HC-05 真机升级、断连恢复、双通道互扰）**未验证**——需上板按
  [docs/user_manual.md](docs/user_manual.md)「蓝牙升级」章节执行

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
