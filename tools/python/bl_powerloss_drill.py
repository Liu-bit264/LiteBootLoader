#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""bl_powerloss_drill.py — 验收 #9：参数区写入中断恢复钻具（阶段 4）

原理：SET_META(bl_request=0) 每条命令触发一次完整的参数区双副本写入
（擦目标页 -> 写 0x24B -> 回读校验，页 62/63 交替，seq+1）。以约 30~50 ms/条
的节奏连续发送形成"写入风暴"，同时用 pyocd 在随机时刻注入系统复位——
CPU 骤停而 Flash 内容保持，等效于写入中途掉电（bl_metadata.c write_copy 的
擦除/写入/回读任一阶段都可能被命中）。

每轮校验恢复不变量（docs/test_plan.md §5 #9）：
  R1 GET_META 可读（至少一个副本有效且被 BL 选中，本副本 CRC 通过）
  R2 seq 单调不减（被中断的写：要么未生效沿用旧副本，要么已完整生效）
  R3 flags bit0 = 0（本钻具只写 0；若曾置 1 也会被 BL 启动时消费清除）
  R4 修复写入：复位后紧接着的 SET_META 成功且 seq+1（中断残留页可复用）

全程结束校验 APP 区未受牵连：app_valid=1 + 按元数据 size/CRC VERIFY 通过
+ JUMP_APP 跳转成功。真实拔电（等效补充，需人工断电）步骤见 test_plan.md §5 #9。

用法（pyocd 与 pyserial 同环境隔离运行）：
  uv run --python 3.12 --with pyserial --with pyocd \
      tools/python/bl_powerloss_drill.py --port COM4 --rounds 10
"""
import argparse
import random
import struct
import subprocess
import sys
import threading
import time

import bl_upgrade as blp

# pyocd 注入命令：全部字面常量，不接收任何外部输入（安全约束）
PYOCD_CMD = ["uv", "run", "--python", "3.12", "--with", "pyocd", "pyocd",
             "reset", "-t", "stm32f103c8",
             "--pack", "E:/Hardware/Keil/Arm/Packs/Keil/STM32F1xx_DFP/2.4.1"]
BURST_DEFAULT = 150          # 每轮 SET_META 条数（约 4~6 s 风暴，覆盖 pyocd 连接耗时）
SR = random.SystemRandom()   # 复位延时用密码学安全随机源


def pyocd_reset() -> bool:
    """注入一次系统复位（板子复位后继续运行）。"""
    try:
        p = subprocess.run(PYOCD_CMD, capture_output=True, text=True,
                           timeout=30, shell=False)
        if p.returncode != 0:
            print(f"    [!] pyocd reset 退出码 {p.returncode}: "
                  f"{(p.stderr or p.stdout).strip()[-160:]}")
        return p.returncode == 0
    except Exception as e:
        print(f"    [!] pyocd reset 异常: {e}")
        return False


def get_meta(bl: blp.BootLoader):
    r = bl.cmd("get_meta")
    if r is None or r["data"][0] != 0:
        return None
    return blp.parse_meta(r["data"])


def one_round(bl: blp.BootLoader, idx: int, burst: int) -> bool:
    """一轮：ensure_bl -> 写入风暴 + 定时复位注入 -> 恢复不变量 -> 修复写入。"""
    if not blp.ensure_bl(bl):
        print(f"[R{idx:02d}] FAIL: 无法进入 BL")
        return False
    m0 = get_meta(bl)
    if m0 is None:
        print(f"[R{idx:02d}] FAIL: 基线 GET_META 失败")
        return False

    # 定时复位：延时后由后台线程注入，主线程继续风暴
    ev = threading.Event()
    delay = SR.uniform(0.3, 0.8)   # pyocd 连接约 1.5~2.5s，复位落在风暴中段

    def fire():
        ok = pyocd_reset()
        if not ok:
            ev.reset_failed = True   # type: ignore[attr-defined]
        ev.set()

    timer = threading.Timer(delay, fire)
    t0 = time.time()
    timer.start()

    n_ok, hit = 0, "burst_done"    # mid_cmd / between / after_cmd / burst_done
    bl.noise = b""                 # 清掉上一轮/进入路径的横幅，避免误判复位命中
    for _ in range(burst):
        if ev.is_set():
            hit = "after_cmd"
            break
        r = bl.cmd("set_meta", b"\x01\x00", timeout=0.4)
        if r is None:
            hit = "mid_cmd"        # 复位打断在途命令（最有价值的命中）
            break
        if bl.saw_reboot_banner():
            hit = "between"        # 复位落在两条命令之间
            break
        n_ok += 1
    timer.join(40)
    hit_failed = getattr(ev, "reset_failed", False)

    time.sleep(0.5)                # 等复位后 BL 完成启动
    m1 = get_meta(bl)
    if m1 is None:
        # 兜底：可能卡在启动窗口之外（APP 在跑），走一次请求回 BL
        if not blp.ensure_bl(bl):
            print(f"[R{idx:02d}] FAIL: 复位后无法恢复会话")
            return False
        m1 = get_meta(bl)
        if m1 is None:
            print(f"[R{idx:02d}] FAIL: 复位后 GET_META 失败（R1 违反）")
            return False

    checks = []
    checks.append(("R1 meta可读", True, f"seq={m1['seq']} copy={m1['active_copy']}"))
    r2 = m0["seq"] <= m1["seq"] <= m0["seq"] + n_ok + 5
    checks.append(("R2 seq单调", r2, f"{m0['seq']} -> {m1['seq']}（风暴完成 {n_ok} 条）"))
    r3 = (m1["flags"] & 1) == 0
    checks.append(("R3 bl_request=0", r3, f"flags={m1['flags']:#010x}"))

    # 修复写入：中断残留页必须可复用
    rr = bl.cmd("set_meta", b"\x01\x00", timeout=2.0)
    m2 = get_meta(bl) if (rr is not None and rr["data"][0] == 0) else None
    r4 = m2 is not None and m2["seq"] > m1["seq"]
    checks.append(("R4 修复写入", r4,
                   "无响应" if rr is None else
                   (f"seq {m1['seq']} -> {m2['seq']}" if m2 else "状态异常")))

    ok = all(c[1] for c in checks)
    print(f"[R{idx:02d}] {'PASS' if ok else 'FAIL'} 复位命中={hit}"
          + ("（pyocd失败!）" if hit_failed else "")
          + f" 风暴{time.time() - t0:.1f}s")
    for name, passed, ev_txt in checks:
        print(f"       {name}: {'✓' if passed else '✗'} {ev_txt}")
    return ok


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    ap = argparse.ArgumentParser(description="验收 #9：参数区写入中断恢复钻具")
    ap.add_argument("--port", default="COM4")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--rounds", type=int, default=10)
    ap.add_argument("--burst", type=int, default=BURST_DEFAULT)
    a = ap.parse_args()

    bl = blp.BootLoader(a.port, a.baud)
    print(f"== 参数区写入中断恢复钻具：{a.rounds} 轮 × {a.burst} 条写风暴 ==")
    results = []
    for i in range(1, a.rounds + 1):
        results.append(one_round(bl, i, a.burst))

    # 收尾：APP 区未受牵连 + 跳转链路完整
    print("== 收尾校验 ==")
    r = bl.cmd("info")
    info_ok = r is not None and r["data"][0] == 0 and r["data"][18] == 1
    print(f"[{'PASS' if info_ok else 'FAIL'}] app_valid=1（复位风暴后 BL 启动自检 CRC 仍通过）")
    m = get_meta(bl)
    verify_ok = False
    if m and m["size"]:
        rv = bl.cmd("verify", struct.pack("<II", m["size"], m["crc"]), timeout=5.0)
        verify_ok = rv is not None and rv["data"][0] == 0
        print(f"[{'PASS' if verify_ok else 'FAIL'}] VERIFY size={m['size']} "
              f"crc={m['crc']:#010x}")
    jump_ok = False
    if info_ok and verify_ok:
        rj = bl.cmd("jump", timeout=2.0)
        if rj is not None and rj["data"][0] == 0:
            time.sleep(0.8)            # 等 BL 50ms 延时 + 九步跳转 + APP 起跑
            rg = bl.cmd("info")
            # 判别：APP 迷你响应器对 GET_INFO 只回 1B RANGE_ERROR；BL 回全量遥测
            jump_ok = rg is not None and rg["data"][0] == 0x03
            print(f"[{'PASS' if jump_ok else 'FAIL'}] JUMP_APP 后对端为 APP 响应器"
                  f"（GET_INFO -> {blp.st_name(rg['data'][0]) if rg else '无响应'}）")
            deadline, text = time.time() + 5.0, ""
            while time.time() < deadline:
                chunk = bl.s.read(256)
                if chunk:
                    text += chunk.decode("utf-8", "replace")
                    if "breathing" in text:
                        break
            if text.strip():
                print(f"       APP 输出: {text.strip()[:60]!r}")
    results.append(info_ok and verify_ok and jump_ok)

    n_pass = sum(results)
    print(f"== 钻具结果：{n_pass}/{len(results)} 通过 ==")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
