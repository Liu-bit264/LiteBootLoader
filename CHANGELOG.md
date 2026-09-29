# 更新日志（Changelog）

本项目的所有显著变更记录于此。格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [SemVer 2.0.0](https://semver.org/lang/zh-CN/)。

English version: [CHANGELOG.en.md](CHANGELOG.en.md)

## [Unreleased]

本段将随合并发布为 **BL 0.3.0**（服务可选挂载改动 core 与默认固件行为，feat→MINOR）。

第二芯片支持包：STM32F411CEU6 最小实现包（ADR-017，CSP 阶段 B）；服务可选挂载与
BL 示例配置最小化（ADR-019）。

### Added

- **F411CEU6 支持包**（`chips/f411ceu6.json` + `port/stm32f4/f411ceu6/`）：仅串口升级 +
  引导跳转，无 OLED/蓝牙/I2C。分区 BL 32K（扇区 0-1）/参数区 2×16K（扇区 2/3，独立
  擦除单元）/APP 448K（扇区 4-7）；时钟 `BL_HSE_MHZ` 支持 8/25 两种晶振（100MHz/3WS，
  PWR VOS Scale 1 前置，HSI 回退）；IWDG 2000/放宽 8000ms（128K 扇区擦除期 CPU 停顿
  无法喂狗，PR/256 一档覆盖两档）；`BL_TRANSPORT_BT_EN=0` 关闭蓝牙通道（槽位保留）
- **LED 状态显示服务**（`services/display_led/`）：无 OLED 支持包的最小显示方案，
  模式表沿用 ADR-008
- **多芯片构建收尾**：`chips/test_chip.py` 自动遍历全部芯片清单并支持非均匀擦除单元
  （F4 显式扇区表两侧比对）；spec 模板 Port/Services/BSP 清单与 include/scatter 路径
  改由 chip.json 驱动；产物按芯片分槽位（`chips/<id>/`、`linker/<id>/`，f103c8t6 保持
  根目录 legacy 槽位且渲染产物逐字节不变）
- **F4 CMSIS 头**（`third_party/CMSIS/`）：Core(M) V5.6.0 + Device STM32F4xx（AC5 兼容，
  Apache-2.0），自本机 STM32Cube_FW_F4_V1.28.3 原样拷贝，SHA-256 核验记录见
  LICENSES.md；新增 `scripts/vendor_copy.py` 拷贝核验工具
- **F411 最小 APP 示例**（`app/examples/f411ceu6_app/`）：呼吸灯 + 升级口响应器
- **`scripts/pyocd_manual_flash.py`**：F411 板寄存器级烧录工具（该板 pyocd 算法路径
  异常的实测解法；校验前先复位清 F4 ART 缓存）

### Changed

- **服务可选挂载（ADR-019）**：唯一强制挂载的服务 = 有线串口通道。
  `core/bl_service_stub.c` 提供 `bl_display`/`bl_debug`/`bl_port_i2c_release` 弱默认
  实现，显示/调试成为链接期可选插件；服务硬件自举移入各服务 init（main.c 只装配
  强制链路）。双板真机验证：F103 selftest 15/15、无显示变体纯串口全流程、F411
  升级/跳转复验
- **BL 示例配置最小化**：F103 默认 BL 配置删除原全功能清单（OLED/蓝牙/I2C 移出构建，
  `BL_TRANSPORT_BT_EN=0`），统一为「最小可用」= `display_led`（LED 状态灯）+
  `debug_uart`（串口日志），与 F411 最小包同型；**APP 示例配置不变**（F103 APP 保留
  OLED 演示，BSP 清单拆分 `bsp_files_bl`/`bsp_files_app`）。OLED/蓝牙作为可选能力
  保留，启用方式见 porting_guide §3.1
- **引脚提升为板级声明（ADR-018）**：`board_config.h` 成为引脚事实唯一出处
  （逻辑 id + `*_PORT` 端口序号 + `*_NUM` 引脚号 + `BL_PIN_LED_ACTIVE_LOW` 极性），
  gpio.c 仅消费（映射表/ops 通路宏驱动，F1 CRL/CRH 配置按 NUM 派生）；
  `chips/test_chip.py` 强制 chip.json `pins` 段与声明一致（含 uart_tx/uart_rx）

### Verification

- `chips/test_chip.py` 12/12 通过（f103c8t6 + f411ceu6 全芯片一致性，含引脚段）
- F103：默认最小 BL 12 180 B ≤ 16K（SHA-256 `ac658278…`）、APP 8 276 B（`d1286297…`）；
  上板 selftest 15/15、升级跳转、无显示变体（12 616 B）纯串口全流程；全功能变体
  15 464 B ≤ 16K（弱桩代价 +64B，`ebb2f93f…`）
- F411：BL 12 584 B ≤ 32K（SHA-256 `4277840d…`，0.3.0）、APP 6 384 B（`fe11b440…`）；上板
  寄存器级烧录（`scripts/pyocd_manual_flash.py`）→ GET_INFO（flash=512KB/UID 正确）→
  升级（448K 擦除 4.21s，IWDG 8s 放宽实测无复位）→ 跳转呼吸灯 → setmeta 回 BL 闭环 →
  擦除中复位注入恢复 → 有效 APP 上电自跳转
- F103 产物基线：引脚抽象前与 0.2.0 基线逐字节一致（`499de4bc…`/`534eb656…`）；
  此后随服务模型/引脚/配置演进，行为等价、尺寸见上
- 签名/哈希校验确认独立迭代（接入点见 docs/partition.md §4 预留说明）

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
