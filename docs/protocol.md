# 通信协议规范（protocol）

> 版本 0.1.0 · 2026-09-25 · 状态：阶段 0 交付，待评审确认
> 关联：[design.md](design.md)（ADR-001/002/003/007） · [partition.md](partition.md)（元数据与命令副作用） · [external_interface.md](external_interface.md)
> 协议版本：`VER = 0x01`（独立于 BL 软件版本，演进规则见 [versioning.md](versioning.md) §4）

## 3. 物理层

| 项 | 值 |
|---|---|
| 接口 | USART1，PA9 = TX，PA10 = RX |
| 波特率 / 格式 | 115200，8N1，无流控 |
| 电平 | TTL 3.3 V（主机侧经 USB-TTL 适配器接入） |
| 字节序 | 多字节**数值字段一律小端**（LE） |

## 4. 帧格式

```text
SOF(2B) | VER(1B) | CMD(1B) | SEQ(1B) | LEN(2B,LE) | DATA(0..256B) | CRC16(2B,LE) | EOF(2B)
```

| 字段 | 说明 |
|---|---|
| SOF | 同步字节序列 `AA 55`（先 0xAA 后 0x55）。注意：SOF/EOF 是**定位标记**，不按数值字段做字节序转换 |
| VER | 协议版本，当前 `0x01`；不符则丢弃该帧并重新同步 |
| CMD | 命令编号（§5）；响应帧为 `CMD | 0x80` |
| SEQ | 会话序号，主机生成并递增（0–255 回绕）；BL 响应**回显**请求 SEQ |
| LEN | DATA 长度（LE16），范围 0–256 |
| DATA | 命令数据，最长 256 B |
| CRC16 | CRC-16/MODBUS（§4.1），覆盖 `VER` 起至 `DATA` 末尾（不含 SOF/EOF/CRC 自身），小端发送（低字节在前） |
| EOF | 结束字节序列 `55 AA`（先 0x55 后 0xAA） |

整帧最大长度：2+1+1+1+2+256+2+2 = **267 B**。

### 4.1 CRC-16/MODBUS 参数

| 项 | 值 |
|---|---|
| 多项式 | 0x8005（反射实现 0xA001） |
| 初值 | 0xFFFF |
| 输入/输出反射 | 是 / 是 |
| 终异或 | 0x0000 |
| 标准校验值 | `"123456789"` → `0x4B37` |

参考实现（上位机与文档验证用）：

```python
def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc  # 发送时小端: crc.to_bytes(2, "little")
```

### 4.2 解析器行为

- 输入为字节流；状态机 `SOF1 → SOF2 → VER → CMD → SEQ → LEN_LO → LEN_HI → DATA → CRC_LO → CRC_HI → EOF1 → EOF2`。
- **帧同步恢复**：任何位置收到非预期字节即回到 `SOF1` 扫描态（对后续字节逐个滑动匹配 `AA 55`）。日志等无关字节因此被自然丢弃，不破坏协议（design.md ADR-009）。
- **非法长度拒绝**：`LEN > 256` → 丢弃并重新同步，不计入会话。
- **版本不符**：`VER != 0x01` → 丢弃并重新同步。
- **CRC 校验失败**：静默丢弃，**不回错误帧**——原因：CMD 字段位于 CRC 覆盖范围内，帧损坏时 CMD 不可信，无法构造正确响应；由主机超时重试机制兜底。错误码 `CRC_ERROR` 仅用于 `VERIFY_APP` 镜像校验失败（§5.5）与上位机本地预检提示。
- **帧内超时**：一帧接收中途超过 2000 ms 无新字节 → 复位解析器，丢弃半帧。（2026-09-26 定版。原 50 ms 阈值会把多块接收的大帧整帧误杀：`bl_protocol_poll` 曾用循环顶部旧时间戳与 feed 中更新的 `s_last_byte` 做无符号减法，SysTick 毫秒边界跨越其间即回绕成极大值立即假触发——大帧 5 块接收、单块 ~15% 跨界概率 → ~56%/帧，小帧单块且收完即回 SOF1 态故从不触发，selftest 遥测 bytetimeout 计数实锤。修复后 poll 现场重读当前时刻；阈值放宽至 2000 ms 容忍 USB/CDC 转发抖动，请求-响应协议下无副作用，主机重试间隔应 ≥2 s。）

## 5. 命令定义

### 5.0 命令编号与状态码总表

| CMD | 名称 | 请求 DATA | 响应 CMD | 响应 DATA |
|---:|---|---|---:|---|
| 0x01 | PING | 空 | 0x81 | status(1) + protocol_ver(1) |
| 0x02 | GET_INFO | 空 | 0x82 | 31 B，见 §5.2 |
| 0x03 | ERASE_APP | 空 | 0x83 | status(1) |
| 0x04 | WRITE_CHUNK | offset(4,LE) + payload(≤252) | 0x84 | status(1) |
| 0x05 | VERIFY_APP | app_size(4,LE) + app_crc32(4,LE) | 0x85 | status(1) + calc_crc32(4,LE) + calc_size(4,LE) |
| 0x06 | SET_META | field_id(1) + value（§5.6） | 0x86 | status(1) |
| 0x07 | GET_META | 空 | 0x87 | 21 B，见 §5.7 |
| 0x08 | JUMP_APP | 空 | 0x88 | status(1) |
| 0x09 | RESET | 空 | 0x89 | status(1) |
| 0x10–0x1F | （预留）OTA 扩展 | — | — | 仅预留编号，本期不实现 |

所有响应 DATA **首字节固定为状态码**；未知命令回 `CMD|0x80` + `STATE_ERROR`（SEQ 照常回显）。

| 状态码 | 名称 | 含义 |
|---:|---|---|
| 0x00 | OK | 成功 |
| 0x01 | CRC_ERROR | 镜像 CRC 校验不匹配（VERIFY_APP） |
| 0x02 | FLASH_ERROR | Flash 驱动层失败（擦/写/读错误） |
| 0x03 | RANGE_ERROR | 地址范围/对齐/长度校验失败（partition.md §2） |
| 0x04 | STATE_ERROR | 状态不允许（如 JUMP_APP 时 APP 无效） |
| 0x05 | TIMEOUT | BL 侧操作超时（保留） |

### 5.1 PING（0x01）

连通性测试。响应 `status + protocol_ver`（当前 `0x01`）。

### 5.2 GET_INFO（0x02）

响应 DATA（31 B，偏移按字节）：

| 偏移 | 长度 | 内容 |
|---:|---:|---|
| 0 | 1 | status |
| 1 | 3 | BL 版本 major/minor/patch（各 1 B） |
| 4 | 12 | 芯片 96 位 UID（`0x1FFFF7E8`，F103 出厂序列号） |
| 16 | 2 | Flash 容量 KiB（`0x1FFFF7E0`，LE16） |
| 18 | 1 | APP 状态：0x00 无效 / 0x01 有效 |
| 19 | 4 | app_size（LE32，0 = 无） |
| 23 | 4 | app_crc32（LE32） |
| 27 | 4 | 元数据 seq（LE32，出厂态 0） |

### 5.3 ERASE_APP（0x03）

整片擦除 APP 区（页 16–61），页间喂狗。幂等：重复调用返回 OK。DATA 必须为空，否则 `RANGE_ERROR`。

### 5.4 WRITE_CHUNK（0x04）

`DATA = offset(LE32) + payload`，`payload ≤ 252 B`（保证 LEN ≤ 256）。

- `offset` 为相对 **APP 基址（0x08004000）** 的偏移，必须 4 字节对齐。
- 写前自动擦除：目标页内存在非 0xFF 字节则先擦该页（页间喂狗）。
- 越界（`offset + payload > 46 KiB`）或不对齐 → `RANGE_ERROR`。
- 幂等：同 offset 重复写直接覆盖。
- 镜像必须先以 0xFF 填充至 4 字节对齐（主机工具负责），全部 chunk 总长 = `app_size`。

### 5.5 VERIFY_APP（0x05）

`DATA = app_size(LE32) + app_crc32(LE32)`（对填充后镜像的期望值）。

处理：BL 按 1 KiB 块计算 `[APP_BASE, APP_BASE+app_size)` 的 CRC-32/ISO-HDLC（块间喂狗），与 `app_crc32` 比较：

- 匹配 → 回 `OK + calc_crc32 + calc_size`，并以新 seq 将 `app_size/app_crc32` **自动持久化**到参数区（partition.md §6.3）。
- 不匹配 → 回 `CRC_ERROR + calc_crc32 + calc_size`，不持久化。
- `app_size == 0` 或 `> 46 KiB` → `RANGE_ERROR`。

### 5.6 SET_META（0x06）

`DATA = field_id(1) + value`：

| field_id | value | 说明 |
|---:|---|---|
| 0x01 | 1 B：0x01 置位 / 0x00 清除 | `bl_request`（partition.md §8 生命周期）；写入即掉电安全持久化 |
| 0x02 | 3 B：major, minor, patch | APP 版本号，本字段独立立即持久化（新 seq） |

未知 field_id → `RANGE_ERROR`；value 长度不符 → `RANGE_ERROR`。

### 5.7 GET_META（0x07）

响应 DATA（21 B）：

| 偏移 | 长度 | 内容 |
|---:|---:|---|
| 0 | 1 | status |
| 1 | 4 | seq（LE32，出厂态 0） |
| 5 | 4 | flags（LE32） |
| 9 | 3 | app_ver major/minor/patch |
| 12 | 4 | app_size（LE32） |
| 16 | 4 | app_crc32（LE32） |
| 20 | 1 | active_copy：0x00 = 副本 A（页 62），0x01 = 副本 B（页 63），0xFF = 无有效副本 |

### 5.8 JUMP_APP（0x08）

按 [partition.md](partition.md) 元数据 + 实时重算执行完整校验（MSP 范围、Reset Handler 范围与 Thumb 位、app_size、CRC32）：

- 任一失败 → `STATE_ERROR`，不跳转。
- 全部通过 → 回 `OK`，延时 ~50 ms（保证主机收到响应）后执行 AGENTS.md §5.1 九步跳转（IWDG 不关，跳转前喂狗一次）。

### 5.9 RESET（0x09）

回 `OK` 后延时 100 ms 执行 `NVIC_SystemReset`（IWDG 保持运行，等效看门狗复位）。用于主机侧主动复位重测。

## 6. SEQ 语义、幂等性与超时

- **SEQ**：主机逐命令递增（0–255 回绕）；BL 只做回显，不维护去重表。
- **幂等性**：全部命令设计为可重复执行（重 PING 无副作用、重擦页无害、重写覆盖、重验重算、重复 JUMP 校验失败无害），因此**重复包/超时重传直接重新执行**，不做去重。
- **主机侧超时与重试**（工具实现约定，阶段 3 落地）：单命令响应超时 1000 ms，重试 3 次；`ERASE_APP` 因整片擦除耗时（46 页 × 20–40 ms ≈ 1–2 s）单独放宽为 5000 ms。
- **BL 侧**：帧内 2000 ms 无新字节复位解析器（§4.2）；升级模式无会话超时（design.md ADR-003）。

## 7. 示例帧（字节级，CRC 为实测值）

以下 CRC 均按 §4.1 参考实现计算并程序验证过。

### 7.1 PING 请求

覆盖体（CRC 范围）：`01 01 01 00 00`（VER, CMD, SEQ, LEN=0）→ CRC = `0xFC49`

```text
AA 55 01 01 01 00 00 49 FC 55 AA
└SO┘ └VER┘└CMD┘└SEQ┘└LEN─┘└CRC─┘└SO┘
```

### 7.2 PING 响应

覆盖体：`01 81 01 02 00 00 01`（LEN=2，DATA = status 0x00 + protocol_ver 0x01）→ CRC = `0x69E8`

```text
AA 55 01 81 01 02 00 00 01 E8 69 55 AA
```

### 7.3 WRITE_CHUNK 请求（offset=0，payload 8 B `DE AD BE EF 12 34 56 78`）

覆盖体：`01 04 02 0C 00 00 00 00 00 DE AD BE EF 12 34 56 78`（LEN=0x000C=12，DATA = offset 4 B + payload 8 B）→ CRC = `0xC9E8`

```text
AA 55 01 04 02 0C 00 00 00 00 00 DE AD BE EF 12 34 56 78 E8 C9 55 AA
```

### 7.4 WRITE_CHUNK 响应

覆盖体：`01 84 02 01 00 00`（DATA = status 0x00）→ CRC = `0xACA1`

```text
AA 55 01 84 02 01 00 00 A1 AC 55 AA
```

## 8. 异常恢复

| 场景 | 行为 |
|---|---|
| 帧损坏（CRC 错/半帧/长度非法） | 静默丢弃 + 帧同步恢复；主机超时重试 |
| 升级中途掉电 | 镜像破坏 → 重启后 CRC 校验失败 → 升级模式；重新升级即可（partition.md §7） |
| 升级中途 IWDG 复位 | 同上 |
| VERIFY 失败 | 不持久化，主机可继续补写/重传后重新 VERIFY |
| JUMP 后 APP 跑飞（未喂狗） | IWDG 2 s 复位 → BL 重启判定（APP CRC 仍有效则再跳；反复跑飞属 APP 缺陷） |

## 9. 上位机工具与 VOFA+

### 9.1 Python 升级工具（阶段 3 已交付 `tools/python/bl_upgrade.py` v1.0.0）

```bash
# 依赖隔离运行（本机约定：Miniforge base 不装包，见 AGENTS.md §3）
# 一键升级（从任意状态：对端是 BL 直接升；是 APP 则自动"请求回 BL"再升级）
uv run --python 3.12 --with pyserial tools/python/bl_upgrade.py \
    upgrade app.bin --port COM4
# 流程检验（15 步硬件在环 selftest）
uv run --python 3.12 --with pyserial tools/python/bl_upgrade.py \
    selftest --port COM4
# 单命令：ping / info / meta / erase / verify <size> <crc_hex> / jump / reset / ...
```

工具行为：读入镜像 → 0xFF 填充至 4 字节对齐 → `ensure_bl` 探测（GET_INFO：OK=BL 直接升；
RANGE_ERROR=APP 响应器，自动 SET_META bl_request=1 等复位；无响应等 2.2 s 重试 ×3）→
ERASE_APP → 逐 WRITE_CHUNK（DATA 256 B，payload 252 B）→ VERIFY_APP（自动携带 size/CRC）
→ 提示 JUMP_APP。所有命令带重试层（§6）：超时重发 ≤3 次，间隔 2.2 s（≥ BL 帧内 2000 ms
超时复位窗口）。串口枚举用 pyserial（Windows 形如 `COM4`）。

### 9.2 VOFA+ RawData 手动发帧模板

VOFA+ 仅用于**日志观察**与 **RawData 通道手动发 hex 帧**调试，不是正式升级器（定位说明见 [vofa_plus.md](vofa_plus.md)，首跑配置与全命令帧速查表见 `tools/vofa+/README.md`、`tools/vofa+/rawdata_frames.md`）。串口配置 115200 8N1。可直接粘贴的帧（CRC 为实测值，改任意字节需用 §4.1 参考实现重算）：

```text
PING 请求     : AA 55 01 01 01 00 00 49 FC 55 AA
GET_INFO 请求 : AA 55 01 02 01 00 00 49 B8 55 AA
RESET 请求    : AA 55 01 09 01 00 00 4B 9C 55 AA
```
