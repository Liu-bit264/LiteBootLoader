#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""third_party 原样拷贝工具（CSP 步骤，porting_guide.md §2 第 2 步）。

将上游文件**逐字节**拷入本仓 third_party/ 并输出 SHA-256 核验记录
（LICENSES.md 表格数据源）。拷贝后立即读回比对哈希，任何不一致即报错——
保证「原样拷贝、未做任何修改」的可证明性。

用法：
  python scripts/vendor_copy.py --src-dir <上游目录> --dst-dir third_party/CMSIS f1.h f2.h ...
  python scripts/vendor_copy.py --src-dir <上游目录> --dst-dir third_party/CMSIS \
      --map "上游名=仓内名" ...        # 个别文件需改名时

退出码：0 全部核验一致；1 存在不一致或源文件缺失。
"""

import argparse
import hashlib
import shutil
import sys
from pathlib import Path


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    h.update(p.read_bytes())
    return h.hexdigest()


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="原样拷贝上游文件并输出 SHA-256 核验")
    ap.add_argument("--src-dir", required=True)
    ap.add_argument("--dst-dir", required=True)
    ap.add_argument("files", nargs="+",
                    help="文件名或 上游名=仓内名 映射")
    args = ap.parse_args(argv)

    src_dir, dst_dir = Path(args.src_dir), Path(args.dst_dir)
    dst_dir.mkdir(parents=True, exist_ok=True)
    rc = 0
    for item in args.files:
        src_name, _, dst_name = item.partition("=")
        dst_name = dst_name or src_name
        src, dst = src_dir / src_name, dst_dir / dst_name
        try:
            src_bytes = src.read_bytes()
        except OSError as exc:
            print(f"[vendor] 源文件不可读 {src}: {exc}", file=sys.stderr)
            rc = 1
            continue
        dst.write_bytes(src_bytes)
        if sha256(dst) != hashlib.sha256(src_bytes).hexdigest():
            print(f"[vendor] 核验失败（哈希不一致）: {dst}", file=sys.stderr)
            rc = 1
            continue
        print(f"[vendor] IDENTICAL {dst_name} {sha256(dst)}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
