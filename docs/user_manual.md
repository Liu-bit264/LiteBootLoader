# LiteBootLoader 用户手册

> 适用版本：BL 0.2.0 · 2026-09-27 · 面向使用者（非移植开发者）
> 开发/设计细节见文末「深入阅读」；本手册只讲"怎么用"。

## 1. 这是什么

LiteBootLoader（BL）是烧录在 STM32F103C8T6 Flash 头部的引导程序。上电后它先运行，
决定"跳进应用（APP）还是停在升级模式等主机发新固件"，并提供通过**串口（USART1 有线）
或蓝牙（HC-05 → UART2）**升级 APP 的完整通道，还能查询 OTA 状态（BL/APP 版本、
APP 有效性、蓝牙连接状态）。

Flash 分区：

| 区域 | 地址范围 | 大小 | 说明 |
|---|---|---|---|
| BootLoader | 0x08000000–0x08003FFF | 16 KiB | 本体，升级流程**永不擦写** |
| APP | 0x08004000–0x0800F7FF | 46 KiB | 你的应用，升级对象 |
| 参数区 | 0x0800F800–0x0800FFFF | 2 KiB | 双副本元数据（APP 大小/CRC/请求标志），掉电安全 |

## 2. 硬件准备

| 项 | 连接/配置 |
|---|---|
| 串口 | USART1：PA9(TX)/PA10(RX)，115200，8N1，无流控；经 USB-TTL 或 DAPLink CDC 接电脑（本机为 COM4） |
| 蓝牙（可选，0.2.0 起） | HC-05：VCC 5V 共地，RXD←PA2、TXD→PA3，STATE→PB0、EN→PB1；需**一次性 AT 配置**数据模式到 115200（[dev/bluetooth_notes.md](dev/bluetooth_notes.md) §5），配对 PIN 默认 1234 |
| LED | PC13，低电平点亮（核心板板载） |
| OLED | 0.96" SSD1306，软件 I2C：SCL=PB8，SDA=PB9（可选，不影响升级） |
| 调试器 | DAPLink/ST-Link 接 SWD，仅首次烧录 BL 或救砖时需要 |
| 供电 | 常规 3.3 V；IWDG 约 2 s，正常固件都会自动喂狗，无需关心 |

## 3. 首次使用：烧录 BootLoader

BL 本体用调试器烧一次即可，之后升级 APP 全走串口。当前交付二进制：

- `bootloader.bin`：15,324 B，SHA-256 `499de4bc…1c8861`（0.2.0 蓝牙通道 + OTA_QUERY 版；完整值以交付记录为准）
- APP 示例 `app/examples/f103c8t6_app/app.bin`：8,152 B，SHA-256 `534eb656…ff2dd81`

```bash
# pyocd 烧录 BL：flash 与 reset 必须分开调用（pyocd 的 reset 是独立子命令，
# flash 命令不接受 -c "reset"；烧完不复位芯片会停在暂停态看似"没反应"）
uv run --python 3.12 --with pyocd pyocd flash \
    --target stm32f103c8 \
    --pack "E:/Hardware/Keil/Arm/Packs/Keil/STM32F1xx_DFP/2.4.1" \
    --base-address 0x08000000 bootloader.bin
uv run --python 3.12 --with pyocd pyocd reset \
    --target stm32f103c8 \
    --pack "E:/Hardware/Keil/Arm/Packs/Keil/STM32F1xx_DFP/2.4.1"
```

验证：打开串口（COM4，115200），按一下复位键，应看到启动横幅
`LiteBL v0.2.0 … upgrade mode`（首次无 APP 时），LED 1 Hz 慢闪。

## 4. 上电后发生什么

```text
上电/复位 → 读参数区（双副本取最新有效）
  ├─ bl_request=1（APP 请求回 BL）→ 清除标志 → 进入升级模式
  ├─ APP 校验通过（栈顶/复位向量/大小/CRC 全对）
  │     → 打开 3 秒等待窗口（期间收到合法帧头则转升级模式）
  │     → 窗口结束跳转 APP（LED 常亮 ≥300 ms → 跳）
  └─ APP 无效/无 APP → 进入升级模式，无限等待（无自动超时）
```

- 升级模式中 IWDG 照常喂狗，BL 本身不会超时退出；离开方式只有三种：
  升级后 JUMP_APP、发 RESET 命令、断电重上。
- **APP 请求回 BL**：APP 运行中发 `SET_META(0x01,1)` 后自复位（示例 APP 已内置）；
  该标志掉电保存，直到被 BL 消费。也可由主机直接对 BL 发 RESET。

## 5. 指示说明（以固件实现为准）

### LED（PC13）

| 模式 | 节奏 | 含义 |
|---|---|---|
| 慢闪 | 亮 0.5 s / 灭 0.5 s | 等待升级 |
| 快闪 | 亮 0.1 s / 灭 0.1 s | 正在升级（最近收到合法帧） |
| 双闪 | 亮 0.2 / 灭 0.2 / 亮 0.2 / 灭 1.4 | APP 无效或校验失败 |
| 常亮 | 持续 | 即将跳转 APP |
| 三短闪 | 0.1×3 亮灭循环 | 致命错误 |

### OLED（如有）

BL 模式（上电 3 秒窗口 / 升级等待期）：

```text
LiteBL 0.2.0          ← BL 版本
F103C8  CLK:72M       ← 芯片与时钟（HSE 失败时显示 CLK:8M(HSI)）
MODE:WAIT/HOST        ← 当前状态；升级中显示 UPG:xx%
CRC:OK / CRC:--       ← 最近一次 APP CRC 校验结果
BT:OK / BT:-- WDG:ON  ← 蓝牙连接状态（STATE 引脚）+ IWDG 状态
…rx/vf 计数           ← 各通道累计接收字节 / 有效帧数
```

跳转后由 APP 接管（示例 APP）：

```text
A:APP v0.1.0          ← A: 前缀即 APP 在跑
F103C8 CLK:72M
RUN:breathing
WDG:ON
B[##################--] ← 呼吸亮度条（4 秒一个呼吸周期）
```

### 串口日志

与协议共用 USART1，默认 INFO 级，编译期可用 `BL_LOG_DISABLE`/`BL_LOG_LEVEL` 关闭/调级。
协议活跃期间（最近 10 s 有合法帧）日志自动静音，属正常。

## 6. 日常操作：升级 APP

一条命令，从任意状态（BL 或 APP）直接升级（命令在主仓根目录运行；工具位于独立上位机仓）：

```bash
uv run --python 3.12 --with pyserial ../LiteBootUpgrader/bl_upgrade.py \
    upgrade 你的APP.bin --port COM4
```

工具自动完成：探测对端 →（若 APP 在跑）请求其复位回 BL → 擦除 → 分块写入
（每块 252 B）→ VERIFY 校验（zlib CRC32）→ 报告"APP 就绪"。之后：

```bash
uv run --python 3.12 --with pyserial ../LiteBootUpgrader/bl_upgrade.py jump --port COM4   # 跳转
# 或 reset —— 复位后 BL 校验 APP 有效也会自动跳转
```

> **图形界面**：双击 `../LiteBootUpgrader/bl_upgrade_gui.bat`（或
> `uv run --python 3.12 --with pyserial ../LiteBootUpgrader/bl_upgrade_gui.py`）——
> 一键升级（进度条）/ 跳转 / 复位 / PING，操作日志实时滚动；底部勾选「高级模式」
> 重启后获得全量功能（OTA 查询、META、ERASE、监听、原始发帧等）。

### 蓝牙升级（0.2.0 起）

1. 模块一次性配置：USB-TTL 接 HC-05，KEY/EN 上电拉高进 AT 模式（38400），发
   `AT+UART=115200,0,0`，复位（[dev/bluetooth_notes.md](dev/bluetooth_notes.md) §5 有逐步命令）。
2. PC 与模块配对（PIN 1234），系统出现蓝牙 SPP 串口（形如 COM5）。
3. 升级/查询与有线完全同法，只是端口选蓝牙 COM 口；CLI 可加 `--conn bt`
   （打开失败自动重试），GUI 在「连接类型」下拉选「蓝牙」。

```bash
uv run --python 3.12 --with pyserial ../LiteBootUpgrader/bl_upgrade.py \
    ota --port COM5        # OTA 状态查询：BL/APP 版本、APP 有效性、通道、蓝牙连接
```

**成功判据**：`verify: OK crc=… size=…`；跳转后串口出现 APP 横幅、LED 呈 APP 行为。

> **通道边界（真机实测）**：蓝牙只在 **BL 升级模式**应答——跳进 APP 后蓝牙静默
> （APP 响应器仅实现于有线 USART1，UART2 随九步跳转反初始化且 APP 不再初始化）。
> 因此「从 APP 请求回 BL」需走有线或复位板子；蓝牙适合对**已在升级模式的板子**
> 做免接触升级（以及 OTA 状态查询、擦写校验等全部 BL 命令）。

### 全部子命令

| 子命令 | 作用 |
|---|---|
| `upgrade <bin>` | 一键升级（见上） |
| `ota` | OTA 状态查询（0.2.0 起） |
| `ping` / `info` / `meta` | 握手 / 读 BL 信息与遥测 / 读参数区元数据 |
| `erase` | 擦除 APP 区（约 1.1 s） |
| `verify <size> <crc_hex>` | 按给定 size+CRC 校验 APP |
| `jump` / `reset` | 跳 APP / 复位 |
| `setmeta <field> <value>` | 写元数据字段（0x01=bl_request） |
| `selftest` | 15 步升级流程硬件在环自检 |
| `listen <秒>` / `raw <hex>` | 监听串口输出 / 发送原始字节（调试用） |

串口互斥：工具连接期间 VOFA+/串口助手必须关闭，反之亦然（有线与蓝牙口同理——同一时间
只应有一台主机与 BL 会话，另一通道的并发帧会被 BL 丢弃并重试兜底）。

## 7. VOFA+ 手动调试

定位是"看日志 + 手动发单条帧"，不是升级器。首跑步骤与全部命令的 hex 帧见
[tools/vofa+/README.md](../tools/vofa+/README.md) 与
[tools/vofa+/rawdata_frames.md](../tools/vofa+/rawdata_frames.md)（CRC 已预计算，直接粘贴）。

## 8. 故障排查

| 症状 | 可能原因 | 处理 |
|---|---|---|
| 工具全部"无响应" | 串口被别的程序占用；DAPLink 会话遗留暂停态 | 关闭占用者；pyocd 命令补 `-c "reset"` 后再试 |
| 蓝牙口打不开/反复断 | 模块未上电/未配对；SPP 重连窗口 | 重新配对（PIN 1234）；`--conn bt` 已含打开重试，仍失败重插模块 |
| 蓝牙口刚配对打不开（FileNotFoundError） | 模块刚上电 RFCOMM 未就绪 | 重试打开即可（实测出现，2 秒后即恢复） |
| 蓝牙口全"无响应" | 模块数据模式不是 115200；或板子在 APP 态 | 前者按 bluetooth_notes.md §5 重新 AT 配置；后者是设计边界——蓝牙只在 BL 升级模式应答（见上「通道边界」），有线 PING 后 `upgrade` 请求回 BL 即可 |
| 偶发命令超时但重试成功 | USB-CDC 抖动触发 BL 帧内 2 s 超时 | 正常，工具自动重发（≤3 次） |
| `verify 失败: CRC_ERROR` | 镜像损坏/升级中断导致内容不完整 | 重新执行 `upgrade`（会整片重擦重写） |
| JUMP 返回 STATE_ERROR | APP 无效（CRC/向量不合法） | 重新升级；LED 双闪与此同因 |
| LED 三短闪 | 致命错误 | 看 OLED/串口日志定位；必要时重烧 BL |
| 板子 2 s 周期性复位 | APP 没喂狗（IWDG 约束） | 检查 APP 喂狗；BL 侧不受影响 |
| 时钟显示 CLK:8M(HSI) | HSE 未起振 | 检查晶振/焊接；功能仍可用，仅主频降为 8M |
| 升级中断电/复位 | 写入不完整，VERIFY 会拦下 | 重新上电后再 `upgrade` 即可；BL 与参数区不受影响 |

## 9. 注意事项与已知限制

- 镜像上限 46 KiB（0xB800），超出会被工具本地拒绝；4 字节对齐由工具自动补 0xFF。
- 升级路径**永远不会**擦写 BL 区与参数区（固件强制地址校验，越界返回 RANGE_ERROR）——有线与蓝牙通道同界。
- **固件未签名/认证**：升级链路只有 CRC 完整性校验（帧 CRC16 + 镜像 CRC32），USART1/蓝牙上任何主机都可烧写任意镜像——这是当前阶段的明确非目标，生产部署前需评估（认证接入点：storage 校验链 + 元数据结构）。
- 手动发帧（VOFA+/raw 命令）必须整帧一次发出：帧内字节间隔超过 2 s，BL 会丢弃半帧重新同步。
- 蓝牙链路吞吐低于有线（46 KiB 约 10–20 s，视 SPP 实况），升级中断连后重连重跑 `upgrade` 即可（整片重来）。
- 中断恢复是"整片重来"而非断点续传（46 KiB 有线全程约 15 s，可接受）。
- 跳转 APP 后 OLED 保持 BL 最后一帧，属设计行为（APP 可自行接管刷新）。
- 协议帧 CRC 用 CRC16/MODBUS，APP 镜像校验用 CRC-32/ISO-HDLC（zlib 兼容）。

## 10. 深入阅读

| 想了解 | 看这里 |
|---|---|
| 协议细节（帧格式/命令/状态码） | [docs/protocol.md](protocol.md) |
| 分区与参数区掉电安全设计 | [docs/partition.md](partition.md) |
| 架构与移植（换芯片） | [docs/architecture.md](architecture.md) / [docs/porting_guide.md](porting_guide.md) |
| 测试与验收 | [docs/dev/test_plan.md](dev/test_plan.md) |
| 对端引脚/接口速查 | [docs/external_interface.md](external_interface.md) |
| 蓝牙模块参数与一次性配置 | [docs/dev/bluetooth_notes.md](dev/bluetooth_notes.md) |
