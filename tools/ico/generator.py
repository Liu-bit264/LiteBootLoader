#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ICO 生成器：由 PNG 列表生成 ICO 文件。

优先使用 Pillow：自动解码、LANCZOS 缩放并编码为 32bpp BMP-DIB 条目（兼容性最好）。
Pillow 不可用时回退为直接嵌入 PNG 条目：要求 PNG 数量与尺寸一一对应且实际
尺寸完全一致（Windows Vista 及以上支持 PNG 条目；部分老工具不识别）。

写入前会用同目录 parser.py 对生成内容做结构自校验（头/偏移/边界）；
目标文件已存在时自动备份（--no-backup 关闭）。

用法：
    python generator.py -o out.ico --sizes 16,32,48,256 icon.png   # 单源缩放到多尺寸（需 Pillow）
    python generator.py -o out.ico --sizes 16,32 16.png 32.png     # PNG 与尺寸一一对应

退出码：0 成功；1 生成/校验失败；2 参数错误。
"""

from __future__ import annotations

import argparse
import importlib.util
import shutil
import struct
import sys
import time
from pathlib import Path

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
_ICONDIR = struct.Struct("<HHH")
_ICONDIRENTRY = struct.Struct("<BBBBHHII")

try:
    from PIL import Image
    HAVE_PILLOW = True
except ImportError:
    Image = None
    HAVE_PILLOW = False

if HAVE_PILLOW:
    try:
        RESAMPLE = Image.Resampling.LANCZOS
    except AttributeError:  # Pillow < 9.1
        RESAMPLE = Image.LANCZOS


class IcoGenerateError(Exception):
    """输入不合法或生成失败。"""


def _load_parser_module():
    parser_path = Path(__file__).with_name("parser.py")
    spec_obj = importlib.util.spec_from_file_location("bl_ico_parser", parser_path)
    module = importlib.util.module_from_spec(spec_obj)
    spec_obj.loader.exec_module(module)
    return module


def read_png_size(path: Path) -> tuple:
    data = path.read_bytes()
    if data[:8] != PNG_SIGNATURE:
        raise IcoGenerateError(f"{path}: 不是 PNG 文件（签名不符）")
    if len(data) < 24 or data[12:16] != b"IHDR":
        raise IcoGenerateError(f"{path}: 缺少 IHDR，无法读取尺寸")
    width, height = struct.unpack(">II", data[16:24])
    return width, height


def _dib_entry_from_image(img) -> bytes:
    """把 Pillow RGBA 图像编码为 ICO 条目：BITMAPINFOHEADER + XOR(BGRA) + AND 掩码。"""
    img = img.convert("RGBA")
    width, height = img.size
    stride = width * 4
    raw = img.tobytes("raw", "BGRA")  # 自上而下
    xor = b"".join(raw[y * stride:(y + 1) * stride]
                   for y in range(height - 1, -1, -1))  # ICO 行序自下而上
    and_stride = ((width + 31) // 32) * 4  # AND 掩码 1bpp，行按 32bit 对齐
    and_mask = bytes(and_stride * height)  # 带 alpha 时掩码全 0
    header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0,
                         len(xor) + len(and_mask), 0, 0, 0, 0)
    return header + xor + and_mask


def entries_with_pillow(sizes, inputs) -> list:
    sources = list(inputs) if len(inputs) == len(sizes) else [inputs[0]] * len(sizes)
    entries = []
    for (width, height), src in zip(sizes, sources):
        img = Image.open(src)
        if img.size != (width, height):
            img = img.resize((width, height), RESAMPLE)
        entries.append((width, height, _dib_entry_from_image(img)))
    return entries


def entries_without_pillow(sizes, inputs) -> list:
    if len(inputs) != len(sizes):
        raise IcoGenerateError(
            "未安装 Pillow：PNG 数量必须与 --sizes 一致且实际尺寸完全相同"
            f"（当前 {len(inputs)} 个 PNG / {len(sizes)} 个尺寸）；"
            "安装 Pillow 后支持单源自动缩放")
    entries = []
    for (width, height), src in zip(sizes, inputs):
        actual = read_png_size(src)
        if actual != (width, height):
            raise IcoGenerateError(
                f"{src}: 实际尺寸 {actual[0]}x{actual[1]} 与目标 "
                f"{width}x{height} 不一致（未安装 Pillow，无法缩放）")
        entries.append((width, height, src.read_bytes()))
    return entries


def build_ico(entries) -> bytes:
    """entries: [(width, height, blob), ...]，blob 为 DIB 或完整 PNG 字节。"""
    count = len(entries)
    if not 1 <= count <= 65535:
        raise IcoGenerateError(f"条目数量 {count} 不合法")
    data_offset = 6 + 16 * count
    directory = bytearray(_ICONDIR.pack(0, 1, count))
    body = bytearray()
    for width, height, blob in entries:
        directory += _ICONDIRENTRY.pack(
            0 if width >= 256 else width,
            0 if height >= 256 else height,
            0, 0, 1, 32, len(blob), data_offset + len(body))
        body += blob
    return bytes(directory + body)


def validate_ico(data: bytes) -> dict:
    return _load_parser_module().parse_ico_bytes(data)


def _parse_sizes(raw: str) -> list:
    sizes = []
    for token in raw.split(","):
        token = token.strip().lower()
        if not token:
            continue
        if "x" in token:
            w_text, _, h_text = token.partition("x")
        else:
            w_text = h_text = token
        try:
            width, height = int(w_text), int(h_text)
        except ValueError:
            raise IcoGenerateError(f"无法解析尺寸 '{token}'（应为 32 或 32x32 形式）")
        if not (1 <= width <= 256 and 1 <= height <= 256):
            raise IcoGenerateError(f"尺寸 {width}x{height} 超出 ICO 支持的 1..256 范围")
        sizes.append((width, height))
    if not sizes:
        raise IcoGenerateError("--sizes 至少需要一个尺寸")
    return sizes


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="由 PNG 列表生成 ICO")
    ap.add_argument("inputs", nargs="+",
                    help="PNG 源文件；1 个 = 自动缩放到全部尺寸（需 Pillow），"
                         "N 个 = 与尺寸一一对应")
    ap.add_argument("-o", "--output", required=True, help="输出 .ico 路径")
    ap.add_argument("--sizes", default="16,32,48,256",
                    help="目标尺寸，逗号分隔（默认 16,32,48,256）")
    ap.add_argument("--no-backup", action="store_true", help="覆盖前不备份")
    ap.add_argument("--no-validate", action="store_true", help="跳过写入前的内部校验")
    args = ap.parse_args(argv)

    try:
        sizes = _parse_sizes(args.sizes)
        inputs = [Path(p) for p in args.inputs]
        for p in inputs:
            if not p.is_file():
                raise IcoGenerateError(f"输入 PNG 不存在: {p}")
        if HAVE_PILLOW:
            entries = entries_with_pillow(sizes, inputs)
        else:
            entries = entries_without_pillow(sizes, inputs)
        data = build_ico(entries)
        if not args.no_validate:
            result = validate_ico(data)
            if result["errors"]:
                raise IcoGenerateError(
                    "生成内容自校验失败: " + "; ".join(result["errors"]))
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        backup_path = None
        if out.exists() and not args.no_backup:
            stamp = time.strftime("%Y%m%d-%H%M%S")
            backup_path = str(out.with_name(out.name + f".bak-{stamp}"))
            shutil.copy2(out, backup_path)
        out.write_bytes(data)
    except IcoGenerateError as exc:
        print(f"[generator] 错误: {exc}", file=sys.stderr)
        return 1

    size_desc = ", ".join(f"{w}x{h}" for w, h in sizes)
    mode = "Pillow 缩放(32bpp BMP-DIB)" if HAVE_PILLOW else "PNG 直嵌(无 Pillow)"
    print(f"[generator] 已生成 {out}（{len(sizes)} 个条目: {size_desc}；模式: {mode}）")
    if backup_path:
        print(f"[generator] 原文件已备份为 {backup_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
