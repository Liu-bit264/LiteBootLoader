# RawData 命令帧模板（LiteBootLoader）

全部帧按 docs/protocol.md §2 帧格式生成，CRC16/MODBUS 已预计算，可直接复制到
VOFA+ RawData 引擎发送区（HEX 格式）发送。

帧格式：

```
SOF(AA 55) | VER(01) | CMD | SEQ | LEN(LE16) | DATA(0..256B) | CRC16(LE16) | EOF(55 AA)
```

- CRC 覆盖 `VER` 至 `DATA` 末尾；多字节字段小端。
- 以下模板统一使用 `SEQ=01`。BL 回显请求 SEQ，不校验 SEQ 值本身；
  若手动连发同一条命令，建议手动递增 SEQ 便于对照日志，但保持 01 也能正常工作（全命令幂等）。
- 改动 DATA 内容后 CRC 必须重算，算法见文末。

## 命令帧一览

| 命令 | CMD | DATA | 帧十六进制（可直接复制） |
|---|---:|---|---|
| PING | 0x01 | 空 | `AA 55 01 01 01 00 00 49 FC 55 AA` |
| GET_INFO | 0x02 | 空 | `AA 55 01 02 01 00 00 49 B8 55 AA` |
| ERASE_APP | 0x03 | 空 | `AA 55 01 03 01 00 00 48 44 55 AA` |
| WRITE_CHUNK（示例：4B 写入 offset 0） | 0x04 | offset(LE32)+payload | `AA 55 01 04 01 08 00 00 00 00 00 DE AD BE EF EE 56 55 AA` |
| VERIFY_APP（示例：512B 全 FF） | 0x05 | size(LE32)+crc32(LE32) | `AA 55 01 05 01 08 00 00 02 00 00 9F C3 7B BD 97 19 55 AA` |
| SET_META bl_request=1 | 0x06 | `01 01` | `AA 55 01 06 01 02 00 01 01 F7 8E 55 AA` |
| SET_META bl_request=0 | 0x06 | `01 00` | `AA 55 01 06 01 02 00 01 00 36 4E 55 AA` |
| GET_META | 0x07 | 空 | `AA 55 01 07 01 00 00 49 74 55 AA` |
| JUMP_APP | 0x08 | 空 | `AA 55 01 08 01 00 00 4A 60 55 AA` |
| RESET | 0x09 | 空 | `AA 55 01 09 01 00 00 4B 9C 55 AA` |

> VOFA+ 发送时带不带空格均可（HEX 模式自动忽略空白）。

## 响应帧判定

响应 CMD = 请求 CMD | 0x80，`DATA[0]` = 状态码：

| 状态码 | 名称 | 含义 |
|---:|---|---|
| 0x00 | OK | 成功 |
| 0x01 | CRC_ERROR | 帧 CRC 校验失败（此类帧被静默丢弃，不会有响应） |
| 0x02 | FLASH_ERROR | Flash 操作失败 |
| 0x03 | RANGE_ERROR | 地址/长度/字段越界 |
| 0x04 | STATE_ERROR | 当前状态不允许该操作（如 APP 无效时 JUMP_APP） |
| 0x05 | TIMEOUT | 内部超时 |

示例：PING 的正常响应为 `AA 55 81 01 01 02 00 <CRC_L> <CRC_H> 55 AA`，
DATA = `00 01`（OK + 协议版本 0x01）。

## 各命令要点

- **WRITE_CHUNK**：DATA 前 4 字节为 APP 区内偏移（LE32，须 4 字节对齐），
  之后为 payload（≤252B）。示例帧向 offset 0 写入 `DE AD BE EF`。
  换 offset 或 payload 后必须重算 CRC。BL 对同一页的重复写入不会重复擦除
  （页擦除位图），但跨 chunk 修改已写区域不会被检测——顺序写入即可。
- **VERIFY_APP**：DATA = 期望 size(LE32) + 期望 CRC32(LE32)。
  CRC32 算法为 CRC-32/ISO-HDLC（与 Python `zlib.crc32` 一致）。
  示例帧 = 512B 全 0xFF 的校验值（size=0x200，crc=0xBD7BC39F），
  擦除后未写入时发送它应返回 OK。校验通过后 BL 自动将 size/crc 持久化到参数区。
- **SET_META**：DATA = field(1B) + value(1B)。field `0x01` = bl_request。
  置 1 后由 APP 复位、BL 消费（进入升级模式时清除）；在 BL 内置 0 可手动清除。
- **JUMP_APP**：APP 校验（MSP/复位向量/Thumb 位/size/CRC）失败回 STATE_ERROR 不跳；
  成功回 OK 后约 50 ms 执行跳转。
- **ERASE_APP**：擦除全部 46 个 APP 页，约 1~2 s，期间无响应属正常。

## CRC16/MODBUS 参考实现（Python）

```python
def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else (crc >> 1)
    return crc
```

校验向量："123456789" → 0x4B37。CRC 低字节在前（LE16）。

帧构造示例（PING）：

```
头 = 01 01 01 00 00        (VER, CMD, SEQ, LEN_L, LEN_H)
CRC = crc16_modbus(头) = 0xFC49 → 写为 49 FC
帧 = AA 55 + 头 + 49 FC + 55 AA
```
