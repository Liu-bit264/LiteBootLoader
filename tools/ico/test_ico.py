#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ICO 工具基本测试：直接 `python test_ico.py` 运行（unittest，无外部依赖；
Pillow 存在时额外覆盖缩放路径）。"""

import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import generator as ico_generator  # noqa: E402
import parser as ico_parser  # noqa: E402


def make_png(width: int, height: int) -> bytes:
    """手工构造一个最小合法 PNG（RGBA/8bit，无 Pillow 依赖）。"""

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    raw = b"".join(b"\x00" + b"\xDE\xAD\xBE\xEF" * width for _ in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def make_dib_entry(width: int, height: int) -> bytes:
    xor = b"\x11\x22\x33\x44" * (width * height)
    and_stride = ((width + 31) // 32) * 4
    return (struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0,
                        len(xor) + and_stride * height, 0, 0, 0, 0)
            + xor + bytes(and_stride * height))


def make_ico_bytes(entries, corrupt_type: bool = False) -> bytes:
    count = len(entries)
    out = bytearray(struct.pack("<HHH", 0, 2 if corrupt_type else 1, count))
    offset = 6 + 16 * count
    body = bytearray()
    for width, height, blob in entries:
        out += struct.pack("<BBBBHHII", width, height, 0, 0, 1, 32,
                           len(blob), offset + len(body))
        body += blob
    return bytes(out + body)


class IcoParserTest(unittest.TestCase):
    def test_valid_ico(self):
        data = make_ico_bytes([
            (16, 16, make_dib_entry(16, 16)),
            (32, 32, make_png(32, 32)),
        ])
        result = ico_parser.parse_ico_bytes(data)
        self.assertTrue(result["header_valid"])
        self.assertEqual(result["image_count"], 2)
        self.assertEqual(result["errors"], [])
        self.assertEqual(result["images"][0]["format"], "BMP(DIB)")
        self.assertEqual(result["images"][0]["bpp"], 32)
        self.assertEqual(result["images"][0]["bmp"]["height"], 16)
        self.assertEqual(result["images"][1]["format"], "PNG")
        self.assertEqual(result["images"][1]["png"]["width"], 32)

    def test_bad_header_type(self):
        data = make_ico_bytes([(16, 16, make_dib_entry(16, 16))], corrupt_type=True)
        result = ico_parser.parse_ico_bytes(data)
        self.assertFalse(result["header_valid"])
        self.assertTrue(any("type" in e for e in result["errors"]))

    def test_entry_out_of_bounds(self):
        good = make_ico_bytes([(16, 16, make_dib_entry(16, 16))])
        result = ico_parser.parse_ico_bytes(good[:-4])  # 截断条目数据
        self.assertTrue(result["errors"])

    def test_file_too_small(self):
        with self.assertRaises(ico_parser.IcoParseError):
            ico_parser.parse_ico_bytes(b"\x00\x00")


class IcoGeneratorTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_build_and_validate(self):
        png16 = self.dir / "16.png"
        png32 = self.dir / "32.png"
        png16.write_bytes(make_png(16, 16))
        png32.write_bytes(make_png(32, 32))
        sizes = [(16, 16), (32, 32)]
        if ico_generator.HAVE_PILLOW:
            entries = ico_generator.entries_with_pillow(sizes, [png32])  # 单源缩放
        else:
            entries = ico_generator.entries_without_pillow(sizes, [png16, png32])
        data = ico_generator.build_ico(entries)
        result = ico_generator.validate_ico(data)
        self.assertEqual(result["errors"], [])
        self.assertEqual(result["image_count"], 2)
        for image in result["images"]:
            self.assertEqual(image["bpp"], 32)
            self.assertLessEqual(image["data_offset"] + image["data_length"],
                                 len(data))

    def test_fallback_size_mismatch_rejected(self):
        png = self.dir / "16.png"
        png.write_bytes(make_png(16, 16))
        with self.assertRaises(ico_generator.IcoGenerateError):
            ico_generator.entries_without_pillow([(32, 32)], [png])

    def test_bad_size_token(self):
        with self.assertRaises(ico_generator.IcoGenerateError):
            ico_generator._parse_sizes("abc")


if __name__ == "__main__":
    unittest.main(verbosity=2)
