# 外部接口清单（external_interface）

> 版本 0.1.0 · 2026-09-25 · 状态：阶段 0 交付，待评审确认
> 定位：对外可见的硬件与软件接口**索引式清单**；细节以各专文为准，本文保证与专文一致。

## 1. 硬件接口

### 1.1 引脚分配

| 引脚 | 功能 | 说明 | 出处 |
|---|---|---|---|
| PA9 | USART1_TX | 升级协议 + 日志（复用策略见 [design.md](design.md) ADR-009） | protocol.md §3 |
| PA10 | USART1_RX | 升级协议 | protocol.md §3 |
| PC13 | LED | 低电平点亮，非阻塞模式见 design.md ADR-008 | — |
| PB9 | OLED_SDA | 软件 I2C | §1.2 |
| PB8 | OLED_SCL | 软件 I2C | §1.2 |

### 1.2 I2C / OLED

- 默认软件 I2C：PB8=SCL、PB9=SDA，开漏输出 + 外部上拉（4.7 kΩ），速率以时序参数配置。
- 保留硬件 I2C1 重映射选项（I2C1 引脚重映射至 PB8/PB9），经编译期配置切换，`bl_i2c_ops` 接口不变。
- 器件：0.96" SSD1306（I2C 地址 0x3C），驱动位于 `bsp/oled_ssd1306/`。

### 1.3 USART1 物理参数

115200 bps，8N1，无流控；主机侧经 3.3 V USB-TTL 适配器，Windows 端口形如 `COM3`。

## 2. 通信协议接口（详见 [protocol.md](protocol.md)）

| 项 | 摘要 |
|---|---|
| 帧格式 | `SOF(AA 55) VER CMD SEQ LEN(LE16) DATA(0..256) CRC16(LE16) EOF(55 AA)` |
| 帧校验 | CRC-16/MODBUS（poly 0x8005/0xA001，init 0xFFFF，check 0x4B37），覆盖 VER…DATA |
| 镜像校验 | CRC-32/ISO-HDLC（zlib 兼容，check 0xCBF43926），对 0xFF 填充至 4 字节对齐的镜像 |
| 命令 | PING 0x01 / GET_INFO 0x02 / ERASE_APP 0x03 / WRITE_CHUNK 0x04 / VERIFY_APP 0x05 / SET_META 0x06 / GET_META 0x07 / JUMP_APP 0x08 / RESET 0x09；响应 = CMD\|0x80 |
| 状态码 | OK 0x00 / CRC_ERROR 0x01 / FLASH_ERROR 0x02 / RANGE_ERROR 0x03 / STATE_ERROR 0x04 / TIMEOUT 0x05 |
| 预留 | 命令 0x10–0x1F 预留 OTA 扩展（未实现） |
| 协议版本 | VER = 0x01 |

## 3. 软件抽象接口

### 3.1 transport 抽象（`core/bl_transport.h`）

```c
typedef struct {
    void (*init)(void);
    bool (*send)(const uint8_t *buf, uint32_t len);
    bool (*recv)(uint8_t *buf, uint32_t len, uint32_t *out_len);   /* 非阻塞 */
} bl_transport_ops;
```

首实现 UART；**CAN / SPI / I2C 扩展方法**：实现同签名 ops 并注册即可，protocol 层不感知通道。选型约束：面向字节流的通道直接适配；面向报文的通道（CAN）需在适配层做流拆包（阶段外文档化）。

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
void bl_display_user_page(void);                  /* 弱符号：用户自检页扩展点（AGENTS §8.1） */

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

`bl_state_t`：WAITING / UPGRADING / APP_INVALID / JUMPING / FAULT（对应 design.md ADR-008 五种 LED 模式）。

## 4. 参数区与元数据接口

布局、副本状态机、掉电恢复：见 [partition.md](partition.md) §3–§7（摘要：页 62/63 双副本，magic "BLP1"，seq 单调递增，CRC-32 覆盖 0x00–0x1F）。模块接口 `bl_meta_load / bl_meta_commit_app / bl_meta_set_bl_request / bl_meta_set_app_version` 见 partition.md §10。

## 5. APP 请求进入 BL 的机制

| 项 | 约定 |
|---|---|
| 触发 | 主机向**运行中的 APP** 发 `SET_META(field=0x01, value=1)`（帧协议与 BL 相同）；APP 掉电安全落盘后回 `OK`，约 100 ms 后 `NVIC_SystemReset` |
| 持久性 | 置位即掉电安全落盘（参数区双副本）；清除前的任何复位/掉电均保留请求 |
| 清除时机 | BL 启动读取到 `bl_request=1` → 进入升级模式前消费并清除（partition.md §8） |
| 升级模式 | 停留等待，无自动超时（design.md ADR-003） |

APP 侧约定（阶段 2 示例已实现）：链接至 `0x08004000`、启动设 `SCB->VTOR`、重新 `__enable_irq`（跳转第 4 步关中断）、接管 IWDG 喂狗。APP 升级口响应器（`app_request.c`）只实现 `PING(0x01)` 与 `SET_META(0x06, field=0x01)`，其余命令回 `RANGE_ERROR`；APP 串口提示统一用 `A:` 前缀（BL 日志用 `I:`），banner 形如 `A:APP v0.1.0 running, breathing`。

## 6. 后续 OTA 接入点（只定义，不实现）

1. **通道层**：新增 transport ops 实现（如 BLE/以太网模组桥接成字节流），protocol 与 storage 不变。
2. **命令层**：命令 0x10–0x1F 预留（如分块元数据、差分升级、密钥握手）。
3. **安全扩展位**：元数据 `flags` 高位保留可扩展"镜像签名有效"标志（配合外部校验器，本期不做）。

## 7. 构建与烧录接口（阶段 1 回填）

- BL 产物 `bootloader.bin` ≤ 16 KiB，烧写至 `0x08000000`；APP 产物 `app.bin` ≤ 46 KiB，链接基址 `0x08004000`。
- 构建命令（Keil）：`E:\Hardware\Keil\Keil_v5\UV4\UV4.exe -b <工程>.uvprojx -j0 -o <日志>`，退出码 0/1/≥2 = 无警告/有警告/有错误。
- 烧录命令（OpenOCD 或 Keil）阶段 1 给出可复现步骤。
