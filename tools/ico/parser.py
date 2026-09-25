#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ICO 解析器：读取 ICO 文件，输出尺寸、bpp、偏移、长度与图像信息（JSON）。

校验内容（对应项目规范）：
  - 文件头必须为 00 00 01 00（reserved=0, type=1）
  - 目录项数量与文件大小一致（目录区不越界）
  - 每个条目的 offset + length 不得超出文件边界
  - 识别条目内嵌格式（PNG / BMP-DIB）并读取真实宽高

用法：
    python parser.py <input.ico> [-o out.json] [--compact]

退出码：0 结构校验通过；1 结构错误或读取失败；2 参数错误。
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

SCHEMA = "bl-ico-parse/1"
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
_ICONDIR = struct.Struct("<HHH")          # reserved, type, count
_ICONDIRENTRY = struct.Struct("<BBBBHHII")  # w, h, colors, reserved, planes, bpp, size, offset
_BMP_INFOHEADER = struct.Struct("<IiiHHIIiiII")


class IcoParseError(Exception):
    """ICO 读取或结构错误。"""


def _parse_png_header(blob: bytes) -> dict:
    info = {"signature_valid": blob[:8] == PNG_SIGNATURE}
    if len(blob) >= 24 and blob[12:16] == b"IHDR":
        width, height = struct.unpack(">II", blob[16:24])
        info["width"] = width
        info["height"] = height
        if len(blob) >= 26:
            info["bit_depth"] = blob[24]
            info["color_type"] = blob[25]
    else:
        info["error"] = "缺少有效 IHDR"
    return info


def _parse_bmp_dib(blob: bytes) -> dict:
    info = {}
    if len(blob) < 40:
        info["error"] = f"DIB 头不足 40 字节（实际 {len(blob)}）"
        return info
    (bi_size, bi_width, bi_height, bi_planes, bi_bit_count,
     bi_compression, bi_size_image, _xppm, _yppm, _clr_used, _clr_important
     ) = _BMP_INFOHEADER.unpack_from(blob, 0)
    info["bi_size"] = bi_size
    info["bi_width"] = bi_width
    info["bi_height_raw"] = bi_height
    info["bi_planes"] = bi_planes
    info["bi_bit_count"] = bi_bit_count
    info["bi_compression"] = bi_compression
    info["bi_size_image"] = bi_size_image
    if bi_height % 2 == 0:
        info["width"] = bi_width
        info["height"] = bi_height // 2  # ICO 条目的 biHeight = 图像高 x2（XOR+AND）
        info["note"] = "biHeight 已按 ICO 约定折算（原值为图像高度两倍，含 AND 掩码）"
    else:
        info["error"] = f"biHeight={bi_height} 不是偶数，不符合 ICO DIB 约定"
    return info


def parse_ico_bytes(data: bytes, source: str = "<memory>") -> dict:
    if len(data) < 6:
        raise IcoParseError(
            f"{source}: 文件仅 {len(data)} 字节，不足以容纳 ICO 头（6 字节）")

    reserved, ico_type, count = _ICONDIR.unpack_from(data, 0)
    errors = []
    if reserved != 0:
        errors.append(f"文件头 reserved 应为 0x0000，实际 0x{reserved:04X}")
    if ico_type != 1:
        errors.append(f"文件头 type 应为 1（图标），实际 {ico_type}")
    header_valid = reserved == 0 and ico_type == 1

    images = []
    for index in range(count):
        entry_off = 6 + 16 * index
        if entry_off + 16 > len(data):
            errors.append(
                f"目录项 {index} 越界：需要偏移 {entry_off}..{entry_off + 15}，"
                f"文件仅 {len(data)} 字节")
            break
        (b_w, b_h, color_count, _res, planes, bpp,
         length, offset) = _ICONDIRENTRY.unpack_from(data, entry_off)
        image = {
            "index": index,
            "width": 256 if b_w == 0 else b_w,
            "height": 256 if b_h == 0 else b_h,
            "declared_width_byte": b_w,
            "declared_height_byte": b_h,
            "color_count": color_count,
            "planes": planes,
            "bpp": bpp,
            "data_offset": offset,
            "data_length": length,
        }
        if offset + length > len(data):
            errors.append(
                f"条目 {index}: 数据范围 [{offset}, {offset + length}) "
                f"超出文件大小 {len(data)}")
            image["format"] = "unknown"
        else:
            blob = data[offset:offset + length]
            if blob[:8] == PNG_SIGNATURE:
                image["format"] = "PNG"
                image["png"] = _parse_png_header(blob)
            elif len(blob) >= 40:
                image["format"] = "BMP(DIB)"
                image["bmp"] = _parse_bmp_dib(blob)
            else:
                image["format"] = "unknown"
                errors.append(f"条目 {index}: 数据不足以识别格式（{length} 字节）")
        images.append(image)

    return {
        "schema": SCHEMA,
        "source": source,
        "file_size": len(data),
        "reserved": reserved,
        "icon_type": ico_type,
        "header_valid": header_valid,
        "image_count": count,
        "images": images,
        "errors": errors,
    }


def parse_ico_file(path) -> dict:
    path = Path(path)
    if not path.is_file():
        raise IcoParseError(f"文件不存在: {path}")
    return parse_ico_bytes(path.read_bytes(), str(path))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="解析 ICO 文件为 JSON 并校验结构")
    ap.add_argument("input", help="输入 .ico 文件")
    ap.add_argument("-o", "--output", help="输出 JSON 文件（默认打印到 stdout）")
    ap.add_argument("--compact", action="store_true", help="输出单行 JSON")
    args = ap.parse_args(argv)

    try:
        result = parse_ico_file(args.input)
    except IcoParseError as exc:
        print(f"[parser] 错误: {exc}", file=sys.stderr)
        return 1

    text = json.dumps(result, ensure_ascii=False, indent=None if args.compact else 2)
    if args.output:
        Path(args.output).write_text(text + "\n", encoding="utf-8")
        print(f"[parser] 已写入 {args.output}")
    else:
        print(text)

    if result["errors"]:
        print(f"[parser] 校验发现 {len(result['errors'])} 处结构错误",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
