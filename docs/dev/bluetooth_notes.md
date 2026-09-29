# HC-05 蓝牙模块核实笔记（bluetooth_notes）

> 目的：为「空口蓝牙串口 + OTA」迭代（规划书《空口蓝牙串口及OTA》）提供 HC-05 参数的
> 已核实事实与本项目决策。网络信息一律视为待验证输入，本文所有关键结论均经**至少两个
> 独立来源交叉核对**，来源见文末。核实日期：2026-09-27。
>
> 物料：`E:\hw-tools\projects\Library\Software\HC-05.pdf` 为立创 Wiki 页面的图片型快照
> （无法程序化读取文本），本文内容以同源 Wiki 页面 + 下列在线来源为准。

## 1. 引脚语义（6pin：STATE / RXD / TXD / GND / VCC / EN）

| 引脚 | 方向 | 语义 | 本项目接法 |
|---|---|---|---|
| STATE | 模块输出 | **SPP 连接建立时高电平**，断开时低 | → PB0（MCU 输入读取） |
| RXD | 模块输入 | 模块串口接收（3.3 V 逻辑，**不耐 5V**） | ← PA2（USART2_TX，3.3V 直连） |
| TXD | 模块输出 | 模块串口发送（3.3 V 逻辑） | → PA3（USART2_RX，3.3V 直连） |
| GND | — | 与 MCU **共地** | GND |
| VCC | — | 5 V 供电（板载 LDO 降到 3.3 V），工作电流约 40 mA | 5 V |
| EN / KEY | 模块输入 | 进入 AT 命令模式的控制脚（详见 §2） | → PB1（MCU 输出，**默认低**） |

电平结论：模块逻辑电平 3.3 V，与 STM32F103 直连**无需分压**（双方均为 3.3 V）；
仅当对端是 5 V 逻辑 MCU 时才需要给 RXD 分压。F103 的 PA2/PA3 为 3.3 V 驱动，安全。

## 2. AT 模式进入条件（EN/KEY 时序）——本期最重要结论

- **标准行为**：KEY（即 EN，接 BC417 的 pin 34）在**上电瞬间保持高电平** → 进入 AT 命令
  模式（LED 约 2 s 慢闪），AT 串口固定 **38400 8N1**。上电时为低/悬空 → 数据模式。
- **运行中切换不可靠**：模块已在运行时把 KEY 拉高能否切换到 AT 模式**取决于固件版本**，
  多数克隆板（ZS-040 等）只在**上电时**采样 KEY。因此**固件不得依赖运行时翻转 EN 来
  切模式**——本项目 PB1 默认输出低，仅作为预留控制脚（文档化，不参与运行时模式切换）。
- **克隆板差异（ZS-040 等）**：该类板把引脚标为 EN 且多了一个板上按键；部分板 EN 与
  按键并联，外部直接驱动 EN 未必等价于标准 KEY 板。用按键法（断电→按住→上电→慢闪后
  松开）最可靠。实际模块属于哪种板型，上板时按 LED 慢闪特征判别并回填本文。

## 3. 出厂默认参数（经两来源交叉）

| 项 | 默认值 | 备注 |
|---|---|---|
| AT 命令模式波特率 | **38400** 8N1 | 上电时 KEY 高进入 |
| 数据模式波特率 | **9600** 8N1 | 立创 Wiki 演示同值 |
| 配对 PIN | **1234**（部分板 0000） | 规划书要求 AT 探查确认 |
| 设备名 | HC-05 | `AT+NAME?` 查询 |
| 角色 | 从机（ROLE=0） | 手机/PC 主动连接 |

与规划书一致：实物默认波特率与 PIN 若与上表不符，按 §4 的 AT 探查流程用
`AT+UART?` / `AT+PSWD?` 现场确认，**不得把猜测值写进代码**。

## 4. AT 命令速查（均需 `\r\n` 行尾，串口助手勾选“发送新行”）

```text
AT                     握手，回 OK
AT+VERSION?            固件版本
AT+UART?               查询 波特率,停止位,校验   例：+UART:9600,0,0
AT+UART=115200,0,0     设置 数据模式 115200,1 位停止,无校验
                       （停止位：0=1bit 1=2bit；校验：0=无 1=奇 2=偶）
AT+NAME? / AT+NAME=x   查询/设置蓝牙名
AT+PSWD? / AT+PSWD="1234"  查询/设置配对 PIN
AT+ROLE=0              从机模式（本项目固定从机）
AT+RESET               复位模块
AT+ORGL                恢复出厂
```

## 5. 本项目决策与一次性配置流程

**决策**：模块数据模式一次性配置为 **115200 8N1**，固件 `BL_BT_UART_BAUD=115200u`
（board_config.h 常量）。理由：46 KiB 镜像在 9600 下需约 1 分钟以上，115200 约 6–10 s；
代价仅一次一次性配置。

**一次性配置流程**（USB-TTL 直接接模块，不经过 BL 固件）：

1. USB-TTL 交叉接模块 TXD/RXD，共地，VCC 5V；KEY/EN **上电前拉高**（接 3.3V）。
2. 上电 → LED 约 2 s 慢闪 = AT 模式；串口助手 38400 8N1，勾选发送新行。
3. 发 `AT` 确认回 OK；`AT+UART?` 记录当前值；`AT+UART=115200,0,0` 回 OK。
4. `AT+RESET` 后断电、EN 恢复低/接 PB1，重新上电 → 数据模式 115200，LED 快闪。
5. PC 与模块配对（PIN 1234），成功后系统出现 SPP 出来的 COM 口，即可被
   LiteBootUpgrader 当普通串口使用（`--conn bt`）。

**默认配置未启用**（ADR-019，2026-09-29）：BL 示例配置最小化后 uart2.c 不在默认构建清单，蓝牙作为可选能力保留（0.2.0 HIL 已验证），启用方式见 porting_guide.md §3.1。

**固件侧约束**（源自以上事实）：
- PB1（EN）默认低，模块上电进数据模式；固件不做运行时 AT 切换（§2 不可靠）。
- PB0（STATE）电平 = SPP 连接状态，供 OLED 状态行与 `0x10 OTA_QUERY` 响应上报。

### 5.1 实物实测回填（2026-09-27 本轮 HIL，模块实物 = 6pin 克隆板）

| 项 | 实测值 | 手段 |
|---|---|---|
| 模块固件版本 | `5.1-20230630` | `AT+VERSION?` |
| 设备名 | `HC-05` | `AT+NAME?` |
| 配对 PIN | **`1234`**（与 §3 表一致） | `AT+PSWD?` 实读 |
| 数据模式波特率 | 出厂 **`9600,0,0`** → 已写 **`115200,0,0`** 并回读验证 | `AT+UART?` / `AT+UART=115200,0,0` |
| AT 模式进入 | EN 上电拉高 → 38400 应答正常 | §2 结论成立 |

Windows 侧实测注意：
- 配对后生成出/入两个 SPP 口（实测 COM6/COM7，均可用）；
- 模块刚上电后第一次打开出串口可能瞬时 `FileNotFoundError`（RFCOMM 连接未就绪），
  重试打开即可；
- 「一直在连接」卡配对与 PIN 无关，删除 Windows 里半残的配对记录 + 模块断电重上后重试。

## 6. 链路可靠性与协议衔接

- 帧格式沿用 `SOF AA55 / VER / CMD / SEQ / LEN / DATA / CRC16-MODBUS / EOF 55AA`，
  帧校验由 CRC16 保证；命令全幂等 + 主机侧重试（protocol.md §7），天然覆盖空口的
  丢包/乱序/半包粘包场景。BL 侧字节流空闲 2000 ms 复位半帧（BL_FRAME_BYTE_TIMEOUT_MS）。
- 双通道（USART1 有线 / UART2 蓝牙）由 transport 层活动通道仲裁串行化（architecture.md），
  避免两路字节交错进同一个协议解析器。
- SPP 吞吐实测预期 4–10 KB/s（115200 UART 侧），时延 20–100 ms 量级；现有超时
  （T_DEFAULT 1 s 等）均远大于时延，无需放宽；VOFA+ 亦可连蓝牙 COM 口发 RawData 手动帧。

## 7. 参考实现

`E:\hw-tools\projects\Library\Software\BLE_BT37.exe`（14.2 MB）为规划书指定的蓝牙上位机
参考实现，原作者已同意逆向。本期仅在需要比对连接/握手流程时使用，不作为阻塞项。

## 8. 来源（核实于 2026-09-27）

- 立创开发板 Wiki《HC05蓝牙模块》（规划书指定，HC-05.pdf 同源快照）：
  <https://wiki.lckfb.com/zh-hans/dmx/module/rf/hc05-bluetooth-module.html>
- Techbitar《Modify The HC-05 Bluetooth Module Defaults Using AT》（KEY=pin34 上电拉高、AT 流程）：
  <https://techbitar.com/modify-the-hc-05-bluetooth-module-defaults-using-at-command.html>
- Martyn Currey《Arduino with HC-05 – AT mode》（ZS-040 克隆板 EN/按键差异，最权威实操帖）：
  <https://www.martyncurrey.com/arduino-with-hc-05-bluetooth-module-at-mode/>
- Last Minute Engineers《HC-05 Bluetooth Module》（AT 38400、数据模式 9600、RXD 电平）：
  <https://lastminuteengineers.com/setting-up-hc-05-bluetooth-module-arduino-tutorial/>
- Components101《HC-05 Bluetooth Module》（引脚表、STATE 语义、PIN 1234）：
  <https://components101.com/wireless/hc-05-bluetooth-module>
- Arduino Forum《HC-05 can't enter command mode》（克隆板仅上电采样 KEY 的实证讨论）：
  <https://forum.arduino.cc/t/hc-05-cant-enter-command-mode/1139020>
