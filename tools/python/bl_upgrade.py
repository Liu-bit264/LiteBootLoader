#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""bl_upgrade.py — LiteBootLoader 上位机 v1.0.0（阶段 3 正式工具）

协议见 docs/protocol.md：
  SOF(AA 55) | VER(01) | CMD | SEQ | LEN(LE16) | DATA(0..256B) | CRC16(LE16,MODBUS) | EOF(55 AA)
CRC 覆盖 VER..DATA；响应 CMD = 请求 CMD|0x80，DATA[0] = 状态码。

主机侧重试约定（protocol.md §7）：单命令响应超时 1000 ms（ERASE/VERIFY 5000 ms），
超时后重发至多 3 次，重试间隔 ≥2.1 s（等待 BL 帧内 2000 ms 超时复位解析器）。
upgrade 会自动识别对端：若 APP 正在运行，先走"请求回 BL"流程（SET_META bl_request）
再升级——从任意状态一条命令完成升级。

依赖隔离（AGENTS.md §3 环境约定，勿直接 pip install）：
  uv run --python 3.12 --with pyserial tools/python/bl_upgrade.py selftest --port COM4
"""
import argparse
import struct
import sys
import time
import zlib

try:
    import serial
except ImportError:
    sys.exit("缺少 pyserial：请用 uv run --python 3.12 --with pyserial ... 运行")

SOF = b"\xAA\x55"
EOF = b"\x55\xAA"
VER = 0x01
CMD = {"ping": 0x01, "info": 0x02, "erase": 0x03, "write": 0x04,
       "verify": 0x05, "set_meta": 0x06, "get_meta": 0x07,
       "jump": 0x08, "reset": 0x09}
STATUS = {0x00: "OK", 0x01: "CRC_ERROR", 0x02: "FLASH_ERROR",
          0x03: "RANGE_ERROR", 0x04: "STATE_ERROR", 0x05: "TIMEOUT"}
APP_SIZE = 0xB800          # 46 KiB（board_config.h BL_APP_SIZE）
CHUNK_PAYLOAD = 252        # DATA ≤ 256B，WRITE_CHUNK 头占 4B

# 主机侧重试约定（protocol.md §7）：超时重发 ≤3 次，间隔 ≥2.1s 等 BL 解析器复位
RETRY_ATTEMPTS = 3
RETRY_DELAY = 2.2
T_DEFAULT, T_ERASE, T_VERIFY = 1.0, 5.0, 5.0


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else (crc >> 1)
    return crc


def build_frame(cmd: int, seq: int, data: bytes = b"") -> bytes:
    head = bytes([VER, cmd, seq, len(data) & 0xFF, (len(data) >> 8) & 0xFF])
    c = crc16_modbus(head + data)
    return SOF + head + data + bytes([c & 0xFF, (c >> 8) & 0xFF]) + EOF


def st_name(st: int) -> str:
    return STATUS.get(st, f"0x{st:02X}")


class BootLoader:
    def __init__(self, port: str, baud: int = 115200, pace_ms: int = 0):
        try:
            self.s = serial.Serial(port, baud, timeout=0.05, write_timeout=2.0)
        except serial.SerialException as e:
            sys.exit(f"[X] 打开 {port} 失败：{e}（若 UartAssist 等已占用串口请先关闭）")
        self.buf = b""
        self.seq = 0
        self.noise = b""          # 无法成帧的字节（日志/心跳/重启横幅）
        self.sent_bytes = 0       # 本次连接累计发送字节（含坏帧）
        self.expected_delivered = 0  # 本次连接预期送达的有效帧数
        self.pace = pace_ms       # 命令间延时（ms），用于时序假设验证

    def _noise(self, b: bytes):
        if b:
            self.noise = (self.noise + b)[-4096:]

    def saw_reboot_banner(self) -> bool:
        return b"upgrade mode" in self.noise or b"LiteBL" in self.noise

    def cmd(self, name: str, data: bytes = b"", timeout: float = 1.0):
        """发送命令并等待响应；返回 {'cmd','seq','data'} 或 None（超时）。"""
        self.seq = (self.seq + 1) & 0xFF
        f = build_frame(CMD[name], self.seq, data)
        self.sent_bytes += len(f)
        self.expected_delivered += 1
        self.s.write(f)
        self.s.flush()
        if self.pace:
            time.sleep(self.pace / 1000.0)
        r = self.recv_frame(timeout)
        if r is not None:
            if r["cmd"] != (CMD[name] | 0x80):
                raise RuntimeError(f"响应 CMD 不匹配: {r['cmd']:#04x}")
            if r["seq"] != self.seq:
                print(f"    [!] SEQ 错位：请求 {self.seq}，响应 {r['seq']}（疑似迟到/丢失响应）")
        return r

    def cmd_retry(self, name: str, data: bytes = b"", timeout: float = 1.0,
                  attempts: int = RETRY_ATTEMPTS):
        """带重试的命令：超时后等 ≥2.1s（BL 帧内超时复位半帧）再重发。
        收到状态响应（含错误码）不重试——那是真实答复。"""
        for i in range(attempts):
            r = self.cmd(name, data, timeout=timeout)
            if r is not None:
                return r
            if i < attempts - 1:
                print(f"    [!] {name} 超时，{RETRY_DELAY:.1f}s 后重发（{i + 2}/{attempts}）")
                time.sleep(RETRY_DELAY)
        return None

    def send_raw(self, frame: bytes):
        self.sent_bytes += len(frame)
        self.s.write(frame)
        self.s.flush()

    def recv_frame(self, timeout: float):
        """帧同步解析：跳过日志/心跳噪声，CRC 或边界不合法则逐字节重同步。"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            chunk = self.s.read(256)
            if chunk:
                self.buf += chunk
            while True:
                idx = self.buf.find(SOF)
                if idx < 0:
                    if len(self.buf) > 1:
                        self._noise(self.buf[:-1])
                    self.buf = self.buf[-1:]   # 保留尾字节防 SOF 被截断
                    break
                if idx:
                    self._noise(self.buf[:idx])
                    self.buf = self.buf[idx:]
                if len(self.buf) < 7:
                    break
                ln = self.buf[5] | (self.buf[6] << 8)
                if ln > 256:                   # 非法长度：伪 SOF，丢 1 字节
                    self.buf = self.buf[1:]
                    continue
                total = 11 + ln
                if len(self.buf) < total:
                    break
                frame, self.buf = self.buf[:total], self.buf[total:]
                body = frame[2:7 + ln]
                want = frame[7 + ln] | (frame[8 + ln] << 8)
                if (frame[2] == VER and frame[-2:] == EOF
                        and crc16_modbus(body) == want):
                    return {"cmd": frame[3], "seq": frame[4],
                            "data": frame[7:7 + ln]}
                self._noise(frame)         # 坏帧计入噪声，整帧丢弃即完成重同步
        return None


# ---- 响应解析 ----

def parse_info(d: bytes) -> str:
    if len(d) < 19:
        # 短响应：BL 全量遥测至少 19B；1B 状态响应通常是 APP 迷你响应器所答
        return (f"短响应（{len(d)}B，status={st_name(d[0]) if d else '空'}）"
                f"—— 对端疑似 APP 而非 BL")
    ma, mi, pa = d[1], d[2], d[3]
    uid = bytes(d[4:16])
    flsz = d[16] | (d[17] << 8)
    valid = d[18]
    size, crc, seq = struct.unpack_from("<III", d, 19)
    extra = ""
    if len(d) >= 39:
        rx, vf = struct.unpack_from("<II", d, 31)
        extra = f" rx={rx} vf={vf}"
    if len(d) >= 47:
        cfc, bto = struct.unpack_from("<II", d, 39)
        extra += f" crcfail={cfc} bytetimeout={bto}"
    if len(d) >= 51:
        (tpd,) = struct.unpack_from("<I", d, 47)
        extra += f" pend={tpd}"
    if len(d) >= 67:
        gap, tpend, tstate, tgot = struct.unpack_from("<IIII", d, 51)
        extra += (f" | 超时现场: 饥饿{gap}ms 缓冲{tpend}B 状态{tstate} 已收{tgot}")
    return (f"BL v{ma}.{mi}.{pa} flash={flsz}KB app_valid={valid} "
            f"app_size={size} app_crc={crc:#010x} seq={seq} "
            f"uid={uid.hex().upper()}{extra}")


def parse_meta(d: bytes) -> dict:
    """GET_META 响应 21B：status + seq(4) + flags(4) + ver(3) + size(4) + crc(4) + copy(1)"""
    seq, flags = struct.unpack_from("<II", d, 1)
    ver = (d[9], d[10], d[11])
    size, crc = struct.unpack_from("<II", d, 12)
    return {"seq": seq, "flags": flags, "ver": ver,
            "size": size, "crc": crc, "active_copy": d[20]}


def meta_str(m: dict) -> str:
    return (f"seq={m['seq']} flags={m['flags']:#010x} "
            f"app_ver={m['ver'][0]}.{m['ver'][1]}.{m['ver'][2]} "
            f"size={m['size']} crc={m['crc']:#010x} copy={m['active_copy']}")


def verify_probe(bl: BootLoader, size: int, want_crc: int):
    """发送 VERIFY 并返回 (status, calc_crc, calc_size)。"""
    r = bl.cmd("verify", struct.pack("<II", size, want_crc), timeout=3.0)
    if r is None:
        return None, None, None
    st = r["data"][0]
    calc, csize = struct.unpack_from("<II", r["data"], 1)
    return st, calc, csize


def info_counters(bl: BootLoader):
    """读 GET_INFO 遥测计数器，返回 (rx, vf, crcfail, bytetimeout, pending) 或 None。"""
    r = bl.cmd("info")
    if r is not None and r["data"][0] == 0 and len(r["data"]) >= 51:
        return struct.unpack_from("<IIIII", r["data"], 31)
    return None


def timeout_detail(bl: BootLoader):
    """读最近一次帧内超时现场 [gap_ms, pending, state, got]，异常返回 None。"""
    r = bl.cmd("info")
    if r is not None and r["data"][0] == 0 and len(r["data"]) >= 67:
        return struct.unpack_from("<IIII", r["data"], 51)
    return None


# ---- selftest：升级流程硬件在环检验（二分定位版） ----

def selftest(bl: BootLoader) -> int:
    print(f"== 升级流程检验 selftest（{bl.s.port} @ {bl.s.baudrate}）==")
    results = []

    def step(name, ok, evidence):
        results.append(ok)
        print(f"[{'PASS' if ok else 'FAIL'}] {name}: {evidence}")

    # 运行起始快照：本次运行内做 rx/vf 差值，开机累计历史不影响判读
    c0 = info_counters(bl)
    bl.sent_bytes = 0
    bl.expected_delivered = 0

    # 1. PING
    r = bl.cmd("ping", timeout=1.0)
    step("PING", r is not None and r["data"][0] == 0 and r["data"][1] == VER,
         "无响应" if r is None else
         f"status={st_name(r['data'][0])} proto_ver={r['data'][1]:#04x}")

    # 2. ERASE_APP（46 页，预计 1~2s）
    t0 = time.time()
    r = bl.cmd("erase", timeout=8.0)
    step("ERASE_APP", r is not None and r["data"][0] == 0,
         "无响应" if r is None else
         f"status={st_name(r['data'][0])} 耗时={time.time() - t0:.2f}s")

    # 3. 零写入读路径探针：擦除后对全 0xFF 内容做 VERIFY。
    #    同时覆盖三件事：CRC32 实现与 zlib 一致性 / 擦除有效性 / Flash 读路径。
    for size in (1024, 512):
        want = zlib.crc32(b"\xFF" * size) & 0xFFFFFFFF
        st, calc, csize = verify_probe(bl, size, want)
        if st is None:
            step(f"读路径探针 verify({size},全FF)", False, "无响应")
        else:
            step(f"读路径探针 verify({size},全FF)", st == 0 and calc == want,
                 f"status={st_name(st)} calc_crc={calc:#010x}（zlib 期望={want:#010x}）"
                 f" calc_size={csize}")

    # 4. 最小写入：4B @0（上一轮 252B@0 无响应，先最小化复现）
    pat = bytes((i * 7 + 0x5A) & 0xFF for i in range(512))
    bl.noise = b""
    t0 = time.time()
    r = bl.cmd("write", struct.pack("<I", 0) + pat[0:4], timeout=5.0)
    if r is None:
        verdict = "无响应（5s）"
        verdict += "；检测到启动横幅 → 芯片发生复位（IWDG？）" if bl.saw_reboot_banner() else "；未见启动横幅"
        step("WRITE 4B @0", False, verdict + " —— 探测芯片活性…")
        r2 = bl.cmd("ping", timeout=2.0)
        step("WRITE 后活性探测", r2 is not None,
             "芯片存活" if r2 is not None else "芯片仍无响应")
    else:
        step("WRITE 4B @0", r["data"][0] == 0,
             f"status={st_name(r['data'][0])} 耗时={time.time() - t0:.2f}s")

    # 5. 回读 4B：区分「写入失败」与「读回失真」
    want4 = zlib.crc32(pat[0:4]) & 0xFFFFFFFF
    ff4 = zlib.crc32(b"\xFF" * 4) & 0xFFFFFFFF
    st, calc, csize = verify_probe(bl, 4, want4)
    if st is None:
        step("VERIFY 4B 回读", False, "无响应")
    else:
        hint = "内容=图案 ✓" if calc == want4 else (
            f"内容≠图案（若={ff4:#010x} 则仍为全FF）")
        step("VERIFY 4B 回读", st == 0 and calc == want4,
             f"status={st_name(st)} calc_crc={calc:#010x}（图案={want4:#010x}）—— {hint}")

    # 6. 其余 508B 分块写入
    for off, payload in ((4, pat[4:252]), (252, pat[252:504]), (504, pat[504:512])):
        bl.noise = b""
        t0 = time.time()
        r = bl.cmd("write", struct.pack("<I", off) + payload, timeout=5.0)
        st = None if r is None else r["data"][0]
        if r is None:
            verdict = "无响应（5s）"
            verdict += "；检测到启动横幅 → 芯片发生复位（IWDG？）" if bl.saw_reboot_banner() else "；未见启动横幅"
            step(f"WRITE {len(payload)}B @{off}", False, verdict)
            c1 = info_counters(bl)   # 本次运行内的差值遥测
            if c1 and c0:
                drx, dvf = c1[0] - c0[0], c1[1] - c0[1]
                dcfc, dbto, dtpd = c1[2] - c0[2], c1[3] - c0[3], c1[4] - c0[4]
                lost = bl.sent_bytes - drx
                where = ("全部到达" if lost <= 0 else f"UART/中断层丢失 {lost}B")
                print(f"    遥测: 发 {bl.sent_bytes}B / 实收 {drx}B（{where}），"
                      f"送达 {dvf}/{bl.expected_delivered} 帧，"
                      f"crcfail+{dcfc}，bytetimeout+{dbto}，超时时缓冲有字节+{dtpd}")
            else:
                print("    遥测: GET_INFO 无响应，无法判读")
            det = timeout_detail(bl)
            if det:
                print(f"    超时现场: 饥饿 {det[0]}ms，缓冲待取 {det[1]}B，解析状态 {det[2]}，帧内已收 {det[3]}B")
        else:
            step(f"WRITE {len(payload)}B @{off}", st == 0,
                 f"status={st_name(st)} 耗时={time.time() - t0:.2f}s")

    # 7. 全量 512B 校验 + 持久化
    good = zlib.crc32(pat) & 0xFFFFFFFF
    st, calc, csize = verify_probe(bl, 512, good)
    step("VERIFY 512B 全量", st == 0 and calc == good,
         "无响应" if st is None else
         f"status={st_name(st)} calc_crc={calc:#010x}（zlib={good:#010x}）calc_size={csize}")

    # 8. GET_META 核对持久化
    r = bl.cmd("get_meta")
    m = parse_meta(r["data"]) if r is not None and r["data"][0] == 0 else None
    step("META 持久化", m is not None and m["size"] == 512 and m["crc"] == good,
         meta_str(m) if m else "无响应/状态异常")

    # 9. 越界防护：offset = APP_SIZE 应拒绝
    r = bl.cmd("write", struct.pack("<I", APP_SIZE) + b"\x00" * 4)
    step("WRITE 越界防护", r is not None and r["data"][0] == 0x03,
         "无响应" if r is None else
         f"status={st_name(r['data'][0])}（期望 RANGE_ERROR）")

    # 10. CRC 损坏帧应被静默丢弃
    bl.seq = (bl.seq + 1) & 0xFF
    f = bytearray(build_frame(CMD["ping"], bl.seq))
    f[7] ^= 0xFF                      # 破坏 CRC 低字节
    bl.send_raw(bytes(f))
    r = bl.recv_frame(0.5)
    step("坏 CRC 帧静默丢弃", r is None,
         "0.5s 无响应 ✓" if r is None else f"意外响应 data={r['data'].hex()}")

    # 11. SET_META bl_request 生命周期（解耦判定：只看 flags 往返）
    r1 = bl.cmd("set_meta", bytes([0x01, 0x01]))
    r2 = bl.cmd("get_meta")
    r3 = bl.cmd("set_meta", bytes([0x01, 0x00]))
    r4 = bl.cmd("get_meta")
    if None in (r1, r2, r3, r4):
        step("SET_META bl_request 生命周期", False, "存在无响应步骤")
    else:
        f1 = parse_meta(r2["data"])["flags"]
        f2 = parse_meta(r4["data"])["flags"]
        ok = (r1["data"][0] == 0 and r3["data"][0] == 0
              and (f1 & 1) == 1 and (f2 & 1) == 0)
        step("SET_META bl_request 生命周期", ok,
             f"set→flags={f1:#010x}，clear→flags={f2:#010x}（bit0 1→0）")

    # 12. JUMP_APP：无效 APP 应被拒绝且不跳转
    r = bl.cmd("jump")
    step("JUMP_APP 拒绝无效 APP", r is not None and r["data"][0] == 0x04,
         "无响应" if r is None else
         f"status={st_name(r['data'][0])}（期望 STATE_ERROR，未跳转）")

    n_pass = sum(results)
    print(f"== selftest 结果：{n_pass}/{len(results)} 通过 ==")
    return 0 if all(results) else 1


# ---- 单命令 ----

def ensure_bl(bl: BootLoader) -> bool:
    """确认对端处于 BL；APP 在跑则自动走'请求回 BL'流程（external_interface.md §5）。"""
    print("探测对端…")
    for attempt in range(RETRY_ATTEMPTS):
        r = bl.cmd("info", timeout=T_DEFAULT)
        if r is not None:
            st = r["data"][0]
            if st == 0x00:
                print("对端 = BL ✓")
                return True
            if st == 0x03:      # APP 响应器只认 PING/SET_META，其余回 RANGE_ERROR
                print("对端 = APP，发送 bl_request 请求回 BL…")
                r2 = bl.cmd("set_meta", bytes([0x01, 0x01]), timeout=T_DEFAULT)
                print("APP 已确认请求" if (r2 is not None and r2["data"][0] == 0)
                      else "APP 未按预期确认（继续等待复位）")
                print("等待复位进入 BL（1.6s）…")
                time.sleep(1.6)
                continue        # 重新探测
            print(f"[X] 对端响应异常 status={st_name(st)}")
            return False
        if attempt < RETRY_ATTEMPTS - 1:
            # 无响应可能是 BL 解析器卡在半帧，等帧内超时复位后再试
            print(f"    [!] 探测无响应，{RETRY_DELAY:.1f}s 后重试（{attempt + 2}/{RETRY_ATTEMPTS}）")
            time.sleep(RETRY_DELAY)
    print("[X] 无法确认对端为 BL")
    return False


def cmd_upgrade(bl: BootLoader, path: str) -> int:
    img = open(path, "rb").read()
    if len(img) == 0 or len(img) > APP_SIZE:
        sys.exit(f"[X] 镜像大小 {len(img)} 超出 1B~{APP_SIZE}B")
    if len(img) % 4:
        img += b"\xFF" * (4 - len(img) % 4)     # VERIFY 要求 4 字节对齐
    crc = zlib.crc32(img) & 0xFFFFFFFF
    print(f"镜像 {len(img)}B crc32={crc:#010x}")

    if not ensure_bl(bl):
        return 1

    r = bl.cmd_retry("erase", timeout=T_ERASE)
    print("erase:", st_name(r["data"][0]) if r else "无响应（重试耗尽）")
    if not r or r["data"][0]:
        return 1
    for off in range(0, len(img), CHUNK_PAYLOAD):
        r = bl.cmd_retry("write", struct.pack("<I", off) + img[off:off + CHUNK_PAYLOAD],
                         timeout=2.0)
        if not r or r["data"][0]:
            print(f"\nwrite @{off} 失败: "
                  f"{st_name(r['data'][0]) if r else '无响应（重试耗尽）'}")
            return 1
        print(f"\rwrite {min(off + CHUNK_PAYLOAD, len(img))}/{len(img)}",
              end="", flush=True)
    print()
    r = bl.cmd_retry("verify", struct.pack("<II", len(img), crc), timeout=T_VERIFY)
    if r and r["data"][0] == 0:
        calc, csize = struct.unpack_from("<II", r["data"], 1)
        print(f"verify: OK crc={calc:#010x} size={csize} —— APP 就绪，可 jump")
        return 0
    print("verify 失败:", st_name(r["data"][0]) if r else "无响应（重试耗尽）")
    return 1


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    ap = argparse.ArgumentParser(description="LiteBootLoader 上位机 v1.0.0（阶段 3 正式工具）")
    ap.add_argument("command",
                    choices=["ping", "info", "meta", "erase", "write", "verify",
                             "upgrade", "jump", "reset", "selftest",
                             "listen", "raw", "setmeta"])
    ap.add_argument("arg", nargs="?", help="write/upgrade: 镜像文件；verify: size")
    ap.add_argument("arg2", nargs="?", help="verify: crc32 十六进制")
    ap.add_argument("--port", default="COM4")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--pace", type=int, default=0,
                    help="命令间插入延时（ms），用于时序假设验证")
    a = ap.parse_args()

    if a.command == "selftest":
        sys.exit(selftest(BootLoader(a.port, a.baud, a.pace)))

    bl = BootLoader(a.port, a.baud, a.pace)
    if a.command == "ping":
        r = bl.cmd("ping")
        print("无响应" if r is None else
              f"status={st_name(r['data'][0])} proto_ver={r['data'][1]:#04x}")
    elif a.command == "info":
        r = bl.cmd("info")
        print("无响应" if r is None else parse_info(r["data"]))
    elif a.command == "meta":
        r = bl.cmd("get_meta")
        print("无响应" if r is None else meta_str(parse_meta(r["data"])))
    elif a.command == "erase":
        t0 = time.time()
        r = bl.cmd("erase", timeout=8.0)
        print("无响应" if r is None else
              f"erase: {st_name(r['data'][0])} 耗时={time.time() - t0:.2f}s")
    elif a.command == "upgrade":
        if not a.arg:
            sys.exit("用法：upgrade <镜像文件>")
        sys.exit(cmd_upgrade(bl, a.arg))
    elif a.command == "verify":
        if not a.arg or not a.arg2:
            sys.exit("用法：verify <size> <crc32_hex>")
        r = bl.cmd("verify", struct.pack("<II", int(a.arg, 0), int(a.arg2, 16)),
                   timeout=3.0)
        print("无响应" if r is None else
              f"verify: {st_name(r['data'][0])}")
    elif a.command == "listen":
        secs = float(a.arg) if a.arg else 3.0
        t0 = time.time()
        buf = bytearray()
        while time.time() - t0 < secs:
            chunk = bl.s.read(256)
            if chunk:
                buf += chunk
        text = buf.decode("utf-8", "replace")
        if text.strip():
            print(text, end="")
        print(f"({secs:.0f}s 监听结束)")
    elif a.command == "raw":
        if not a.arg:
            sys.exit("用法：raw <hex 字节，如 42>")
        data = bytes.fromhex(a.arg.replace(" ", ""))
        bl.send_raw(data)
        print(f"已发送 {len(data)}B 原始字节")
    elif a.command == "setmeta":
        if not a.arg or not a.arg2:
            sys.exit("用法：setmeta <field_hex> <value_hex>")
        r = bl.cmd("set_meta", bytes([int(a.arg, 16), int(a.arg2, 16)]))
        print("无响应" if r is None else
              f"set_meta({a.arg},{a.arg2}): {st_name(r['data'][0])}")
        t0 = time.time()
        buf = bytearray()
        while time.time() - t0 < 3.0:
            chunk = bl.s.read(256)
            if chunk:
                buf += chunk
        text = buf.decode("utf-8", "replace")
        if text.strip():
            print("--- 后续串口输出 ---\n" + text, end="")
    elif a.command in ("jump", "reset"):
        follow = 6.0 if a.command == "reset" else 2.5
        r = bl.cmd(a.command, timeout=2.0)
        if r is None:
            print(f"{a.command}: 无响应")
            return
        print(f"{a.command}: {st_name(r['data'][0])}"
              + ("（APP 无效被拒绝）" if r["data"][0] == 0x04 else ""))
        if r["data"][0] != 0x00:
            return
        t0 = time.time()
        buf = bytearray()
        while time.time() - t0 < follow:
            chunk = bl.s.read(256)
            if chunk:
                buf += chunk
        text = buf.decode("utf-8", "replace")
        if text.strip():
            print("--- 串口输出 ---\n" + text, end="")


if __name__ == "__main__":
    main()
