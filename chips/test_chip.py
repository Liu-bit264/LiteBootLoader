#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CSP 测试：chipfill 模板往返 + chip.json↔board_config.h 一致性。

直接 `python chips/test_chip.py` 运行（unittest，无外部依赖）。
- 往返：LiteTools 的 chipfill 渲染的 .spec.json / .sct 必须与仓库内已提交产物一致
  （改了 chips/*.json 或模板后必须重新生成，否则本测试红）。
- 一致性：构建侧事实源（chip.json）与 C 侧唯一出处（board_config.h）的
  分区 / SRAM / 擦除单元 / IWDG 常量必须相等（ADR-015 双事实源纪律）。

工具依赖：uvprojx 工具已外置独立仓 LiteTools（../LiteTools），
可用环境变量 LITETOOLS_UVPROJX 指向其 uvprojx 目录覆盖默认位置。"""

import json
import os
import re
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent          # chips/
ROOT = HERE.parent                              # 仓库根
TOOLS = Path(os.environ.get(
    "LITETOOLS_UVPROJX",
    str(HERE.parent.parent / "LiteTools" / "uvprojx")))
sys.path.insert(0, str(TOOLS))

import chipfill  # noqa: E402
import generator as uv_generator  # noqa: E402

CHIP_JSON = HERE / "f103c8t6.json"
SPEC_TPL_DIR = HERE / "templates"
BOARD_CFG = ROOT / "port" / "stm32f1" / "f103c8t6" / "board_config.h"


def strip_comments(text: str):
    """sct 比较：去注释行、压缩空白（注释允许实现说明性差异）。"""
    return [" ".join(line.strip().split())
            for line in text.splitlines()
            if line.strip() and not line.strip().startswith(";")]


def board_defines(text: str) -> dict:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)   # 去块注释（含跨行）
    defs = {}
    for m in re.finditer(r"^#define\s+(BL_\w+)\s+(.+?)\s*(?://.*)?$", text, re.M):
        defs[m.group(1)] = m.group(2).strip()
    return defs


def _tokenize(expr: str):
    tokens, i = [], 0
    while i < len(expr):
        ch = expr[i]
        if ch.isspace():
            i += 1
        elif ch in "()+-*/%":
            tokens.append(ch)
            i += 1
        elif expr.startswith(("0x", "0X"), i):
            m = re.match(r"0[xX][0-9a-fA-F]+", expr[i:])
            tokens.append(int(m.group(0), 16))
            i += len(m.group(0))
        elif ch.isdigit():
            m = re.match(r"\d+", expr[i:])
            tokens.append(int(m.group(0)))
            i += len(m.group(0))
        else:
            raise ValueError(f"不支持的表达式元素: {expr[i:]!r}")
    return tokens


def _parse_expr(tokens):
    """极简递归下降（乘除优先于加减），仅接受整数与四则/括号——无动态求值。"""
    pos = [0]

    def peek():
        return tokens[pos[0]] if pos[0] < len(tokens) else None

    def take():
        pos[0] += 1
        return tokens[pos[0] - 1]

    def factor():
        t = peek()
        if t == "(":
            take()
            v = expr()
            if peek() != ")":
                raise ValueError("缺少右括号")
            take()
            return v
        if isinstance(t, int) and not isinstance(t, bool):
            take()
            return t
        raise ValueError(f"意外记号: {t!r}")

    def term():
        v = factor()
        while peek() in ("*", "/", "%"):
            op = take()
            r = factor()
            v = v * r if op == "*" else v // r if op == "/" else v % r
        return v

    def expr():
        v = term()
        while peek() in ("+", "-"):
            op = take()
            r = term()
            v = v + r if op == "+" else v - r
        return v

    v = expr()
    if pos[0] != len(tokens):
        raise ValueError("表达式有多余记号")
    return v


def resolve_macro(defs: dict, name: str) -> int:
    """解析 BL_ 宏为整数：迭代展开宏引用后做受限四则求值（受控仓内输入）。"""
    expr = name
    for _ in range(8):   # 迭代展开宏引用，防循环
        nxt = re.sub(r"\bBL_\w+\b", lambda m: "(" + defs[m.group(0)] + ")", expr)
        if nxt == expr:
            break
        expr = nxt
    expr = re.sub(r"(\d)[uU]", r"\1", expr)   # 去整型后缀
    if re.search(r"[A-Za-z_]", re.sub(r"0[xX][0-9a-fA-F]+", "", expr)):
        raise ValueError(f"宏 {name} 展开后含未解析标识符: {expr}")
    return _parse_expr(_tokenize(expr))


class ChipfillRoundtripTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.chip = chipfill.load_chip(CHIP_JSON)

    def test_spec_render_matches_committed(self):
        for target in ("bootloader", "app"):
            rendered = chipfill.build_spec(
                self.chip, target, SPEC_TPL_DIR / f"{target}.spec.template.json")
            committed = json.loads(
                (ROOT / f"{target}.spec.json").read_text(encoding="utf-8"))
            self.assertEqual(
                committed, rendered,
                f"{target}.spec.json 与 chipfill 渲染不一致——"
                f"改 chips/{self.chip['id']}.json 或模板后须重新生成")

    def test_spec_passes_validate(self):
        for target in ("bootloader", "app"):
            uv_generator.validate_spec(chipfill.build_spec(
                self.chip, target, SPEC_TPL_DIR / f"{target}.spec.template.json"))

    def test_sct_render_matches_committed(self):
        for target in ("bootloader", "app"):
            rendered = chipfill.render_sct(
                self.chip, target, TOOLS / "templates" / f"{target}.sct.template")
            committed = (ROOT / "linker" / f"{target}.sct").read_text(encoding="utf-8")
            self.assertEqual(strip_comments(committed), strip_comments(rendered),
                             f"linker/{target}.sct 与渲染不一致，须重新生成")

    def test_unknown_reference_rejected(self):
        bad = json.loads(CHIP_JSON.read_text(encoding="utf-8"))   # 原始清单（无 derived）
        bad.pop("memory", None)
        with self.assertRaises(chipfill.ChipFillError):
            chipfill.build_spec(bad, "bootloader",
                                SPEC_TPL_DIR / "bootloader.spec.template.json")

    def test_malformed_chip_rejected(self):
        # review 2026-09-27 P2：清单字段类型错收敛为 ChipFillError（LiteTools 校验）
        import tempfile
        bad = json.loads(CHIP_JSON.read_text(encoding="utf-8"))
        bad["memory"]["flash_base"] = 0x08000000   # int 而非十六进制字符串
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "bad.json"
            p.write_text(json.dumps(bad), encoding="utf-8")
            with self.assertRaises(chipfill.ChipFillError):
                chipfill.load_chip(p)
        bad2 = json.loads(CHIP_JSON.read_text(encoding="utf-8"))
        bad2["memory"].pop("sram_size", None)
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "bad2.json"
            p.write_text(json.dumps(bad2), encoding="utf-8")
            with self.assertRaises(chipfill.ChipFillError):
                chipfill.load_chip(p)


class BoardConfigConsistencyTest(unittest.TestCase):
    """chip.json（构建侧）↔ board_config.h（C 侧）四类常量一致性（ADR-015）。"""

    @classmethod
    def setUpClass(cls):
        cls.chip = chipfill.load_chip(CHIP_JSON)
        cls.defs = board_defines(BOARD_CFG.read_text(encoding="utf-8"))

    def val(self, name):
        try:
            return resolve_macro(self.defs, name)
        except KeyError as exc:
            self.fail(f"board_config.h 缺少宏 {exc}")

    def hval(self, s):
        return int(s, 16)

    def test_memory(self):
        mem = self.chip["memory"]
        self.assertEqual(self.val("BL_FLASH_BASE"), self.hval(mem["flash_base"]))
        self.assertEqual(self.val("BL_FLASH_SIZE"), self.hval(mem["flash_size"]))
        self.assertEqual(self.val("BL_SRAM_BASE"), self.hval(mem["sram_base"]))
        self.assertEqual(self.val("BL_SRAM_SIZE"), self.hval(mem["sram_size"]))

    def test_partitions(self):
        part = self.chip["partitions"]
        self.assertEqual(self.val("BL_APP_BASE"), self.hval(part["app"]["base"]))
        self.assertEqual(self.val("BL_APP_SIZE"), self.hval(part["app"]["size"]))
        self.assertEqual(self.val("BL_PARAM_BASE"), self.hval(part["params"]["base"]))
        self.assertEqual(self.val("BL_PARAM_SIZE"), self.hval(part["params"]["size"]))
        copies = part["params"]["copies"]
        self.assertEqual(len(copies), 2, "参数区必须双副本")
        self.assertEqual(self.val("BL_PARAM_BASE"), self.hval(copies[0]["base"]))
        self.assertEqual(self.val("BL_PARAM_COPY_SIZE"), self.hval(copies[0]["size"]))
        self.assertEqual(self.val("BL_PARAM_BASE") + self.val("BL_PARAM_COPY_SIZE"),
                         self.hval(copies[1]["base"]))
        self.assertEqual(self.val("BL_PARAM_COPY_SIZE"), self.hval(copies[1]["size"]))

    def test_erase_units(self):
        eu = self.chip["erase_units"]
        self.assertEqual(self.val("BL_ERASE_UNIT_BASE"), self.hval(eu["base"]))
        self.assertEqual(self.val("BL_ERASE_UNIT_SIZE"), self.hval(eu["unit_size"]))
        self.assertEqual(self.val("BL_ERASE_UNIT_COUNT"), eu["count"])

    def test_iwdg(self):
        iwdg = self.chip["iwdg"]
        self.assertEqual(self.val("BL_IWDG_TIMEOUT_MS"), iwdg["normal_ms"])
        self.assertEqual(self.val("BL_IWDG_UPGRADE_TIMEOUT_MS"),
                         iwdg["upgrade_relaxed_ms"])
        self.assertGreaterEqual(iwdg["upgrade_relaxed_ms"], iwdg["normal_ms"],
                                "放宽值不得小于常规值")

    def test_derived_cpu_format(self):
        # 回归（2026-09-27）：cpu 串尺寸字段必须保留 0x 前缀——丢失会让
        # generator._parse_cpu_memory 匹配失败而静默回退默认内存区
        derived = self.chip["derived"]
        self.assertIn("IRAM(0x20000000,0x5000)", derived["cpu_bootloader"])
        self.assertIn("IROM(0x08000000,0x10000)", derived["cpu_bootloader"])
        self.assertIn("IROM(0x08004000,0xB800)", derived["cpu_app"])

    def test_copy_units_independent(self):
        """F1 双副本间隔=1 页必然独立；此处校验副本间隔是擦除单元的整数倍。"""
        self.assertEqual(self.val("BL_PARAM_COPY_SIZE") % self.val("BL_ERASE_UNIT_SIZE"),
                         0, "参数副本间隔必须按擦除单元对齐")


if __name__ == "__main__":
    unittest.main(verbosity=2)
