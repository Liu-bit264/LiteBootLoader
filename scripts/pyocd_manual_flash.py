#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""F411CEU6 手动寄存器级烧录脚本（pyocd）。

背景（2026-09-29 上板实测，test_plan.md §5.1）：本板/芯片组合下 pyocd 的 flash
算法路径（RAM 算法执行）一启动即 FAULT ACK，且 0x1FFF8000 选项字读取 FAULT；
而寄存器级解锁/擦除/编程/回读全程稳定（CPUID=0x410FC241）。本脚本绕开 flash
算法，按 RM0383 §3.7 直接驱动 FLASH 寄存器：解锁 → 按覆盖范围扇区擦除 →
x32 字编程 → 回读 SHA-256 校验 → 复位释放运行。

用法（仓库根目录）：
  uv run --python 3.12 --with pyocd python scripts/pyocd_manual_flash.py \
      chips/f411ceu6/bootloader.bin
  可选：--pack <DFP 版本目录>  --frequency 1000000  --base-address 0x08000000

依赖：pyocd + CMSIS-DAP 探针；扇区表为 F411CEU6 固有布局（与 chips/f411ceu6.json
强制一致，芯片改动需同步）。注意：会话结束前必须复位释放内核，否则串口静默
（halt-on-connect 坑，见 docs/dev/test_plan.md §1）。"""

import argparse
import hashlib
import sys
import time

from pyocd.core.helpers import ConnectHelper

# F411CEU6 扇区表（RM0383 §3.3）：[(起始地址, 大小)]
SECTORS = [
    (0x08000000, 0x4000), (0x08004000, 0x4000),
    (0x08008000, 0x4000), (0x0800C000, 0x4000),
    (0x08010000, 0x10000),
    (0x08020000, 0x20000), (0x08040000, 0x20000), (0x08060000, 0x20000),
]

FLASH_KEYR = 0x40023C04
FLASH_SR = 0x40023C0C
FLASH_CR = 0x40023C10
KEY1, KEY2 = 0x45670123, 0xCDEF89AB
CR_PSIZE_X32 = 0x00000200
CR_PG = 0x00000001
CR_SER = 0x00000002
CR_STRT = 0x00010000
CR_LOCK = 0x80000000
SR_BSY = 0x00010000
SR_ERRMASK = 0x000000F0   # WRPERR/PGAERR/PGPERR/PGSERR


def wait_bsy(t, limit=600):
    for _ in range(limit):
        if not (t.read32(FLASH_SR) & SR_BSY):
            return t.read32(FLASH_SR)
        time.sleep(0.02)
    raise RuntimeError("FLASH BSY 超时")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="F411CEU6 手动寄存器级烧录（绕开 pyocd flash 算法）")
    ap.add_argument("binfile")
    ap.add_argument("--pack", default=r"E:\Hardware\Keil\Arm\Packs\Keil\STM32F4xx_DFP\3.1.1")
    ap.add_argument("--frequency", type=int, default=1000000)
    ap.add_argument("--base-address", default="0x08000000")
    args = ap.parse_args(argv)

    data = open(args.binfile, "rb").read()
    while len(data) % 4:
        data += b"\xff"                       # 字对齐补 0xFF
    base = int(args.base_address, 16)
    print(f"[flash] {args.binfile} {len(data)}B sha256={hashlib.sha256(data).hexdigest()[:16]}…")

    hit = [i for i, (a, s) in enumerate(SECTORS)
           if a < base + len(data) and base + len(data) > a and base < a + s]
    if not hit:
        print("[flash] bin 不在任何扇区范围内", file=sys.stderr)
        return 1

    with ConnectHelper.session_with_chosen_probe(
            target_override="stm32f411ce",
            options={"pack": [args.pack], "frequency": args.frequency}) as session:
        t = session.target
        # 冻结 IWDG 于调试停机（F4 DBGMCU_APB1FZ.DBG_IWDG_STOP=bit11）：板上旧
        # BL 的看门狗会在停机期间照跑，编程到一半触发复位导致回读不一致
        # （2026-09-29 实测踩坑）。F1 的 DBGMCU 基址不同（0xE0002008），本脚本限 F4。
        t.write32(0xE0042008, t.read32(0xE0042008) | (1 << 11))
        t.halt()
        t.write32(FLASH_KEYR, KEY1)
        t.write32(FLASH_KEYR, KEY2)
        if t.read32(FLASH_CR) & CR_LOCK:
            print("[flash] 解锁失败", file=sys.stderr)
            return 1
        for i in hit:
            cr = t.read32(FLASH_CR)
            t.write32(FLASH_CR, (cr & ~0x00000303) | CR_SER | (i << 3))
            t.write32(FLASH_CR, t.read32(FLASH_CR) | CR_STRT)
            sr = wait_bsy(t)
            if sr & SR_ERRMASK:
                print(f"[flash] 扇区 {i} 擦除错误 SR=0x{sr:08X}", file=sys.stderr)
                return 1
            print(f"[flash] 扇区 {i} (0x{SECTORS[i][0]:08X}) 擦除完成")
        t.write32(FLASH_CR, (t.read32(FLASH_CR) & ~0x00000303) | CR_PSIZE_X32 | CR_PG)
        for off in range(0, len(data), 4):
            t.write32(base + off, int.from_bytes(data[off:off + 4], "little"))
            sr = wait_bsy(t, limit=200)
            if sr & SR_ERRMASK:
                print(f"[flash] 编程错误 @0x{base + off:08X} SR=0x{sr:08X}", file=sys.stderr)
                return 1
        t.write32(FLASH_CR, (t.read32(FLASH_CR) & ~(CR_PSIZE_X32 | CR_PG)) | CR_LOCK)
        # 校验前必须先复位再停机：F4 的 ART 加速器会把 DAP 的 flash 读命中到
        # 旧固件运行时填充的缓存行，直接回读会得到"回读不一致"的假失败
        # （2026-09-29 实测踩坑，两次偶发成功只因板子刚断电重启、缓存为空）
        t.reset_and_halt()
        back = bytes(t.read_memory_block8(base, len(data)))
        if back != data:
            print("[flash] 回读不一致！", file=sys.stderr)
            t.reset()
            return 1
        print(f"[flash] 回读校验一致（sha256={hashlib.sha256(back).hexdigest()[:16]}…）")
        t.reset()
        print("[flash] 已复位释放运行")
    return 0


if __name__ == "__main__":
    sys.exit(main())
