# 外部接口清单（external_interface）

> 版本 0.3.0 · 2026-09-25 初版 · 2026-09-29 修订 · 状态：与实现同步
> 定位：对外可见的硬件与软件接口**索引式清单**；细节以各专文为准，本文保证与专文一致。

## 1. 硬件接口

### 1.1 引脚分配

**F103C8T6（参考支持包，端口能力最全）**：

| 引脚 | 功能 | 说明 | 出处 |
|---|---|---|---|
| PA9 | USART1_TX | 升级协议 + 日志（复用策略见 [dev/design.md](dev/design.md) ADR-009） | protocol.md §3 |
| PA10 | USART1_RX | 升级协议 | protocol.md §3 |
| PA2 | USART2_TX | 蓝牙通道（HC-05 RXD）——可选能力，默认示例配置未启用（ADR-019，启用见 porting_guide §3.1） | §1.4 |
| PA3 | USART2_RX | 蓝牙通道（HC-05 TXD） | §1.4 |
| PB0 | BT_STATE | HC-05 STATE 输入：高 = SPP 已连接（输入下拉） | §1.4 |
| PB1 | BT_EN | HC-05 EN 输出：默认低 = 数据模式（运行时翻转进 AT 不可靠，仅预留） | §1.4 |
| PC13 | LED | 低电平点亮，非阻塞模式见 dev/design.md ADR-008 | — |
| PB9 | OLED_SDA | 软件 I2C | §1.2 |
| PB8 | OLED_SCL | 软件 I2C | §1.2 |

**F411CEU6（最小包，ADR-017）**：PA9/PA10 = USART1（115200 8N1，同上）、PC13 = LED
（低电平点亮，开漏）、PC14 = BT_STATE 保留位（NC，未接 HC-05，读值无意义，仅为
OTA_QUERY 字段语义保留；PC14/PC15 在核心板上接了 32.768 kHz RTC（LSE）晶振，不是空闲脚）。
无 USART2/OLED/BT 引脚；`BL_HSE_MHZ=25`（核心板 25 MHz HSE → 100MHz/3WS，HSE 失败回退
HSI 16MHz；8 MHz 分支仅供自换晶振的自制板，未验证）。

### 1.2 I2C / OLED

- 默认软件 I2C：PB8=SCL、PB9=SDA，开漏输出 + 外部上拉（4.7 kΩ），速率以时序参数配置。
- 保留硬件 I2C1 重映射选项（I2C1 引脚重映射至 PB8/PB9），经编译期配置切换，`bl_i2c_ops` 接口不变。
- 器件：0.96" SSD1306（I2C 地址 0x3C），驱动位于 `bsp/oled_ssd1306/`。

### 1.3 USART1 物理参数

115200 bps，8N1，无流控；主机侧经 3.3 V USB-TTL 适配器，Windows 端口形如 `COMx`。

### 1.4 USART2 / 蓝牙 HC-05 物理参数（0.2.0 起，规划书目标 1）

115200 bps，8N1，无流控（与 USART1 同速）。HC-05（BT 2.0 SPP）经 UART2 接入：VCC 5V
共地，RXD←PA2、TXD→PA3（3.3 V 逻辑直连无需分压），STATE→PB0、EN→PB1。模块需**一次性
AT 配置**数据模式到 115200（USB-TTL + `AT+UART=115200,0,0`，AT 模式固定 38400，步骤与
核实来源见 [dev/bluetooth_notes.md](dev/bluetooth_notes.md) §2/§4/§5）；配对 PIN 默认
1234。与 PC 配对后呈现为 SPP 出来的 COM 口，对协议层透明。

## 2. 通信协议接口（详见 [protocol.md](protocol.md)）

| 项 | 摘要 |
|---|---|
| 帧格式 | `SOF(AA 55) VER CMD SEQ LEN(LE16) DATA(0..256) CRC16(LE16) EOF(55 AA)` |
| 帧校验 | CRC-16/MODBUS（poly 0x8005/0xA001，init 0xFFFF，check 0x4B37），覆盖 VER…DATA |
| 镜像校验 | CRC-32/ISO-HDLC（zlib 兼容，check 0xCBF43926），对 0xFF 填充至 4 字节对齐的镜像 |
| 命令 | PING 0x01 / GET_INFO 0x02 / ERASE_APP 0x03 / WRITE_CHUNK 0x04 / VERIFY_APP 0x05 / SET_META 0x06 / GET_META 0x07 / JUMP_APP 0x08 / RESET 0x09 / OTA_QUERY 0x10 / VERIFY_SIGNED 0x11（可选，ADR-020）；响应 = CMD\|0x80 |
| 状态码 | OK 0x00 / CRC_ERROR 0x01 / FLASH_ERROR 0x02 / RANGE_ERROR 0x03 / STATE_ERROR 0x04 / TIMEOUT 0x05 / SIGN_ERROR 0x06（0.4.0，ADR-020） |
| 预留 | 命令 0x12–0x1F 预留 OTA 扩展（0x10 OTA_QUERY、0x11 VERIFY_SIGNED 已实现） |
| 签名验签（可选） | ECDSA P-256 + SHA-256（ADR-020）：`BL_SIGN_EN=1` 的支持包实现 0x11，公钥为部署侧本地头 `bl_sign_pubkey_local.h`（不入库）；现仅 F411 可选（F103 BL 16K 预算不启用）。详见 [protocol.md](protocol.md) §5.11 |
| 芯片身份（0.5.0，ADR-021） | `BL_CHIP_DEVID`（DBGMCU `IDCODE.DEV_ID[11:0]`）随 **GET_INFO 尾 2 B**（响应 67 → 69 B）与 **GET_META 尾 2 B**（21 → 23 B）上报，并落参数区 `0x25-0x26`；识别顺序见 protocol.md §5.2.1，字段与兼容矩阵见 partition.md §4 |
| 协议版本 | VER = 0x01 |

## 3. 软件抽象接口

### 3.1 transport 抽象（`core/bl_transport.h`，0.2.0 多通道化）

flat 函数接口 + 内部通道注册表（仲裁见 [architecture.md](architecture.md) §6.1）：

```c
void bl_transport_init(void);
bool bl_transport_send(const uint8_t *buf, uint32_t len);   /* 路由到活动通道 */
uint32_t bl_transport_recv(uint8_t *buf, uint32_t max);     /* 非阻塞；活动通道锁 */
uint8_t bl_transport_active_channel(void);   /* 0=USART1 有线 1=UART2 蓝牙；0xFF=无 */
uint32_t bl_transport_rx_total(void);                        /* 各通道累计接收之和 */
uint32_t bl_transport_rx_pending(void);
```

通道注册表：`bl_uart`（USART1 有线）/ `bl_uart_bt`（USART2 HC-05）/ `bl_wifi`
（WIFI 占位 stub，规划书目标 2，实接入时补实现并登记通道表）。**CAN / SPI / I2C 扩展
方法**：实现同签名 `bl_uart_ops`（字节流适配）+ 接收统计并登记进 `core/bl_transport.c`
通道表即可，protocol 层不感知通道；面向报文的通道（CAN）需在适配层做流拆包（阶段外文档化）。

### 3.2 storage 抽象（`port/bl_port.h` → `bl_flash_ops`）

方法签名见 [architecture.md](architecture.md) §3。对外保证：升级路径只可达 APP 区；参数区仅 `bl_metadata` 内部可达。

### 3.3 显示/调试服务抽象（ADR-014：`core/bl_display.h` + `core/bl_debug.h`）

```c
/* 显示服务（实现：services/display_oled，OLED+LED 状态指示） */
typedef struct {
    void (*init)(void);
    void (*set_state)(bl_display_state_t state);  /* WAITING/UPGRADING/APP_INVALID/JUMPING/FAULT */
    void (*set_progress)(uint8_t percent);        /* 升级进度 0-100 */
    void (*set_crc_ok)(bool ok);                  /* VERIFY 结果展示 */
    void (*tick)(uint32_t now_ms);                /* 每次主循环调用，内部限频，非阻塞 */
} bl_display_ops;
extern const bl_display_ops bl_display;           /* 链接期绑定，风格同 port ops */
void bl_display_user_page(void);                  /* 弱符号：用户自检页扩展点（AGENTS §7.1） */

/* 调试服务（实现：services/debug_uart，USART1，协议活跃期静音） */
typedef struct {
    void (*init)(void);
    void (*set_level)(bl_debug_level_t level);
    bl_debug_level_t (*get_level)(void);
    void (*log)(bl_debug_level_t level, const char *fmt, ...);  /* 最小子集格式化 */
} bl_debug_ops;
extern const bl_debug_ops bl_debug;
#define BL_LOGE(...) bl_debug.log(BL_DEBUG_ERROR, __VA_ARGS__)   /* 兼容宏，调用点零改动 */
```

换显示/调试实现只替换 services/ 下模块并重接 ops 对象，core 不动。

`bl_state_t`：WAITING / UPGRADING / APP_INVALID / JUMPING / FAULT（对应 dev/design.md ADR-008 五种 LED 模式）。

## 4. 参数区与元数据接口

布局、副本状态机、掉电恢复：见 [partition.md](partition.md) §3–§7（摘要：页 62/63 双副本，magic "BLP1"，seq 单调递增，CRC-32 覆盖 0x00–0x1F）。模块接口 `bl_meta_load / bl_meta_commit_app / bl_meta_set_bl_request / bl_meta_set_app_version` 见 partition.md §10。

## 5. APP 请求进入 BL 的机制

| 项 | 约定 |
|---|---|
| 触发 | 主机向**运行中的 APP** 发 `SET_META(field=0x01, value=1)`（帧协议与 BL 相同）；APP 掉电安全落盘后回 `OK`，约 100 ms 后 `NVIC_SystemReset` |
| 持久性 | 置位即掉电安全落盘（参数区双副本）；清除前的任何复位/掉电均保留请求 |
| 清除时机 | BL 启动读取到 `bl_request=1` → 进入升级模式前消费并清除（partition.md §8） |
| 升级模式 | 停留等待，无自动超时（dev/design.md ADR-003） |

APP 侧约定（阶段 2 示例已实现）：链接至 `0x08004000`、启动设 `SCB->VTOR`、重新 `__enable_irq`（跳转第 4 步关中断）、接管 IWDG 喂狗。APP 升级口响应器（`app_request.c`）只实现 `PING(0x01)` 与 `SET_META(0x06, field=0x01)`，其余命令回 `RANGE_ERROR`；APP 串口提示统一用 `A:` 前缀（BL 日志用 `I:`），banner 形如 `A:APP v0.1.0 running, breathing`。

## 6. OTA 接入点（0.2.0 起部分实现；规划书《空口蓝牙串口及OTA》，ADR-016）

1. **通道层（已实现）**：USART2 + HC-05 蓝牙为 transport 通道 1（`bl_uart_bt`）；WIFI 为
   同签名占位 stub（`bl_wifi`，规划书目标 2 只预留实现层与 API），protocol 与 storage 不变。
2. **命令层（部分实现）**：0x10 OTA_QUERY 已实现（[protocol.md](protocol.md) §5.10，版本/
   有效性/通道/蓝牙连接状态查询）；0x11 VERIFY_SIGNED 已实现（可选签名验签，ADR-020，
   上节）；0x12–0x1F 继续预留（分块元数据、差分升级、密钥握手等）。
3. **认证标志（0.4.0 起落地）**：`flags` 高位保持保留；认证状态由参数区保留区 0x24 的
   auth 字节承载（partition.md §4），启动判定在 `BL_SIGN_EN=1` 时要求 auth=1。
4. **安全边界（不变）**：OTA 路径只可达 APP 区；参数区仅 `bl_metadata` 内部可达
   （[partition.md](partition.md) §1，与通道无关）。

## 7. 构建与烧录接口（阶段 1 回填）

- BL 产物 `bootloader.bin` ≤ 16 KiB，烧写至 `0x08000000`；APP 产物 `app.bin` ≤ 46 KiB，链接基址 `0x08004000`。
- 构建命令（Keil）：`UV4.exe -r <工程>.uvprojx -j0 -o <日志>`（`-r` 全量重建；见 `scripts/build_keil.md`），退出码 0/1/≥2 = 无警告/有警告/有错误。
- 烧录（F103 参考路径）：`pyocd flash --target stm32f103c8 --pack <DFP> --base-address 0x08000000 bootloader.bin` + `pyocd reset`（用户手册 §3）；F411CEU6 走 `scripts/pyocd_manual_flash.py`（寄存器级）。
