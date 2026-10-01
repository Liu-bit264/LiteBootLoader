#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CSP 测试：chipfill 模板往返 + chip.json↔board_config.h 一致性。

直接 `python chips/test_chip.py` 运行（unittest，无外部依赖）。
- 遍历：自动发现 chips/*.json，逐芯片执行全部测试（ADR-015 多芯片）。
- 往返：LiteTools 的 chipfill 渲染的 .spec.json / .sct 必须与仓库内已提交产物一致
  （改了 chips/*.json 或模板后必须重新生成，否则本测试红）。
- 一致性：构建侧事实源（chip.json）与 C 侧唯一出处（board_config.h）的
  分区 / SRAM / 擦除单元 / IWDG 常量必须相等（ADR-015 双事实源纪律）。
- 擦除单元两种形态：uniform=true（均匀 BASE+SIZE*i，F1 页）与 uniform=false
  （显式扇区表 BL_ERASE_UNIT_TABLE，F4 非均匀扇区）。

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

# 负向用例固定夹具（任意芯片皆可，取首个公开支持包）
NEG_CHIP_JSON = HERE / "f103c8t6.json"
SPEC_TPL_DIR = HERE / "templates"


def strip_comments(text: str):
    """sct 比较：去注释行、压缩空白（注释允许实现说明性差异）。"""
    return [" ".join(line.strip().split())
            for line in text.splitlines()
            if line.strip() and not line.strip().startswith(";")]


def board_defines(text: str) -> dict:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)   # 去块注释（含跨行）
    text = text.replace("\\\r\n", " ").replace("\\\n", " ")   # 拼接续行宏（扇区表）
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


def parse_erase_table(expr: str):
    """解析 BL_ERASE_UNIT_TABLE 宏体：{ 起始地址, 大小 } 对的列表。"""
    pairs = re.findall(r"\{\s*(0[xX][0-9a-fA-F]+)u?\s*,\s*(0[xX][0-9a-fA-F]+)u?\s*\}", expr)
    if not pairs:
        raise ValueError(f"擦除单元表为空或无法解析: {expr!r}")
    return [(int(a, 16), int(s, 16)) for a, s in pairs]


def expand_erase_units(eu: dict):
    """chip.json 擦除单元 → [(起始地址, 大小)]（uniform 与显式表两形态统一）。"""
    if eu.get("uniform", False):
        base, size, count = int(eu["base"], 16), int(eu["unit_size"], 16), eu["count"]
        return [(base + i * size, size) for i in range(count)]
    addr, out = int(eu["base"], 16), []
    for u in eu["units"]:
        usize = int(u["size"], 16)
        for _ in range(u["count"]):
            out.append((addr, usize))
            addr += usize
    return out


def discover_chips():
    chips = [chipfill.load_chip(p) for p in sorted(HERE.glob("*.json"))]
    if not chips:
        raise RuntimeError("chips/ 下未发现芯片清单")
    return chips


_PIN_PORTS = {"A": 0, "B": 1, "C": 2, "D": 3, "E": 4, "H": 7}


def parse_pin(name: str):
    """'PC13' -> (端口序号 2, 引脚号 13)（与 board_config *_PORT/*_NUM 约定一致）。"""
    m = re.fullmatch(r"P([A-H])(\d+)", name.strip())
    if not m:
        raise ValueError(f"引脚名不合法: {name!r}（期望 PA9/PC13 形式）")
    return _PIN_PORTS[m.group(1)], int(m.group(2))


def artifact_paths(chip: dict) -> dict:
    """按芯片清单定位产物：f103c8t6 等 legacy 芯片 artifact_dir=""（仓库根槽位），
    新芯片 artifact_dir="chips/<id>"；sct 统一在 build.sct_dir 下。"""
    build = chip["build"]
    adir = build.get("artifact_dir", "").replace("\\", "/")
    base = ROOT / adir if adir else ROOT
    return {
        "spec": {t: base / f"{t}.spec.json" for t in ("bootloader", "app")},
        "sct_dir": ROOT / build["sct_dir"].replace("\\", "/"),
        "board_cfg": ROOT / build["port_dir"].replace("\\", "/") / "board_config.h",
    }


class ChipfillRoundtripTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.chips = discover_chips()
        cls.paths = {c["id"]: artifact_paths(c) for c in cls.chips}

    def test_spec_render_matches_committed(self):
        for chip in self.chips:
            for target in ("bootloader", "app"):
                with self.subTest(chip=chip["id"], target=target):
                    rendered = chipfill.build_spec(
                        chip, target, SPEC_TPL_DIR / f"{target}.spec.template.json")
                    committed = json.loads(
                        self.paths[chip["id"]]["spec"][target].read_text(encoding="utf-8"))
                    self.assertEqual(
                        committed, rendered,
                        f"{chip['id']}/{target}.spec.json 与 chipfill 渲染不一致——"
                        f"改 chips/{chip['id']}.json 或模板后须重新生成")

    def test_spec_passes_validate(self):
        for chip in self.chips:
            for target in ("bootloader", "app"):
                with self.subTest(chip=chip["id"], target=target):
                    uv_generator.validate_spec(chipfill.build_spec(
                        chip, target, SPEC_TPL_DIR / f"{target}.spec.template.json"))

    def test_sct_render_matches_committed(self):
        for chip in self.chips:
            for target in ("bootloader", "app"):
                with self.subTest(chip=chip["id"], target=target):
                    rendered = chipfill.render_sct(
                        chip, target, TOOLS / "templates" / f"{target}.sct.template")
                    committed = (self.paths[chip["id"]]["sct_dir"]
                                 / f"{target}.sct").read_text(encoding="utf-8")
                    self.assertEqual(strip_comments(committed), strip_comments(rendered),
                                     f"{chip['id']}/linker {target}.sct 与渲染不一致，须重新生成")

    def test_unknown_reference_rejected(self):
        bad = json.loads(NEG_CHIP_JSON.read_text(encoding="utf-8"))   # 原始清单（无 derived）
        bad.pop("memory", None)
        with self.assertRaises(chipfill.ChipFillError):
            chipfill.build_spec(bad, "bootloader",
                                SPEC_TPL_DIR / "bootloader.spec.template.json")

    def test_malformed_chip_rejected(self):
        # review 2026-09-27 P2：清单字段类型错收敛为 ChipFillError（LiteTools 校验）
        import tempfile
        bad = json.loads(NEG_CHIP_JSON.read_text(encoding="utf-8"))
        bad["memory"]["flash_base"] = 0x08000000   # int 而非十六进制字符串
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "bad.json"
            p.write_text(json.dumps(bad), encoding="utf-8")
            with self.assertRaises(chipfill.ChipFillError):
                chipfill.load_chip(p)
        bad2 = json.loads(NEG_CHIP_JSON.read_text(encoding="utf-8"))
        bad2["memory"].pop("sram_size", None)
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "bad2.json"
            p.write_text(json.dumps(bad2), encoding="utf-8")
            with self.assertRaises(chipfill.ChipFillError):
                chipfill.load_chip(p)


class BoardConfigConsistencyTest(unittest.TestCase):
    """chip.json（构建侧）↔ board_config.h（C 侧）常量一致性（ADR-015，逐芯片）。"""

    @classmethod
    def setUpClass(cls):
        cls.chips = discover_chips()
        cls.data = {}
        for chip in cls.chips:
            cfg = artifact_paths(chip)["board_cfg"]
            cls.data[chip["id"]] = (chip, board_defines(cfg.read_text(encoding="utf-8")))

    def val(self, chip_id, name):
        defs = self.data[chip_id][1]
        try:
            return resolve_macro(defs, name)
        except KeyError as exc:
            self.fail(f"board_config.h 缺少宏 {exc}")

    def hval(self, s):
        return int(s, 16)

    def test_memory(self):
        for chip, _ in self.data.values():
            with self.subTest(chip=chip["id"]):
                mem = chip["memory"]
                cid = chip["id"]
                self.assertEqual(self.val(cid, "BL_FLASH_BASE"), self.hval(mem["flash_base"]))
                self.assertEqual(self.val(cid, "BL_FLASH_SIZE"), self.hval(mem["flash_size"]))
                self.assertEqual(self.val(cid, "BL_SRAM_BASE"), self.hval(mem["sram_base"]))
                self.assertEqual(self.val(cid, "BL_SRAM_SIZE"), self.hval(mem["sram_size"]))

    def test_partitions(self):
        for chip, _ in self.data.values():
            with self.subTest(chip=chip["id"]):
                cid = chip["id"]
                part = chip["partitions"]
                self.assertEqual(self.val(cid, "BL_APP_BASE"), self.hval(part["app"]["base"]))
                self.assertEqual(self.val(cid, "BL_APP_SIZE"), self.hval(part["app"]["size"]))
                self.assertEqual(self.val(cid, "BL_PARAM_BASE"), self.hval(part["params"]["base"]))
                self.assertEqual(self.val(cid, "BL_PARAM_SIZE"), self.hval(part["params"]["size"]))
                copies = part["params"]["copies"]
                self.assertEqual(len(copies), 2, "参数区必须双副本")
                self.assertEqual(self.val(cid, "BL_PARAM_BASE"), self.hval(copies[0]["base"]))
                self.assertEqual(self.val(cid, "BL_PARAM_COPY_SIZE"), self.hval(copies[0]["size"]))
                self.assertEqual(self.val(cid, "BL_PARAM_BASE") + self.val(cid, "BL_PARAM_COPY_SIZE"),
                                 self.hval(copies[1]["base"]))
                self.assertEqual(self.val(cid, "BL_PARAM_COPY_SIZE"), self.hval(copies[1]["size"]))

    def test_erase_units(self):
        for chip, _ in self.data.values():
            with self.subTest(chip=chip["id"]):
                cid = chip["id"]
                eu = chip["erase_units"]
                units = expand_erase_units(eu)
                # 两形态共同约束：单元拼满 Flash、首单元从 base 起、地址连续
                self.assertEqual(units[0][0], self.hval(eu["base"]))
                self.assertEqual(sum(s for _, s in units), self.hval(chip["memory"]["flash_size"]),
                                 "擦除单元必须恰好拼满 Flash")
                for (a, s), (a2, _s2) in zip(units, units[1:]):
                    self.assertEqual(a + s, a2, "擦除单元必须连续无空洞")
                if eu.get("uniform", False):
                    self.assertEqual(self.val(cid, "BL_ERASE_UNIT_BASE"), self.hval(eu["base"]))
                    self.assertEqual(self.val(cid, "BL_ERASE_UNIT_SIZE"), self.hval(eu["unit_size"]))
                    self.assertEqual(self.val(cid, "BL_ERASE_UNIT_COUNT"), eu["count"])
                else:
                    # F4 等非均匀扇区：C 侧显式表必须与清单展开逐一相等
                    self.assertEqual(self.val(cid, "BL_ERASE_UNITS_UNIFORM"), 0)
                    table = parse_erase_table(self.data[cid][1]["BL_ERASE_UNIT_TABLE"])
                    self.assertEqual(self.val(cid, "BL_ERASE_UNIT_COUNT"), len(table))
                    self.assertEqual(table, units,
                                     "BL_ERASE_UNIT_TABLE 与 chip.json erase_units.units 不一致")

    def test_iwdg(self):
        for chip, _ in self.data.values():
            with self.subTest(chip=chip["id"]):
                cid = chip["id"]
                iwdg = chip["iwdg"]
                self.assertEqual(self.val(cid, "BL_IWDG_TIMEOUT_MS"), iwdg["normal_ms"])
                self.assertEqual(self.val(cid, "BL_IWDG_UPGRADE_TIMEOUT_MS"),
                                 iwdg["upgrade_relaxed_ms"])
                self.assertGreaterEqual(iwdg["upgrade_relaxed_ms"], iwdg["normal_ms"],
                                        "放宽值不得小于常规值")

    def test_derived_cpu_format(self):
        # 回归（2026-09-27）：cpu 串尺寸字段必须保留 0x 前缀——丢失会让
        # generator._parse_cpu_memory 匹配失败而静默回退默认内存区
        for chip, _ in self.data.values():
            with self.subTest(chip=chip["id"]):
                mem, part = chip["memory"], chip["partitions"]
                sb, ss = int(mem["sram_base"], 16), int(mem["sram_size"], 16)
                rb, rs = int(mem["flash_base"], 16), int(mem["flash_size"], 16)
                ab, asz = int(part["app"]["base"], 16), int(part["app"]["size"], 16)
                derived = chip["derived"]
                self.assertIn(f"IRAM(0x{sb:08X},0x{ss:X})", derived["cpu_bootloader"])
                self.assertIn(f"IROM(0x{rb:08X},0x{rs:X})", derived["cpu_bootloader"])
                self.assertIn(f"IROM(0x{ab:08X},0x{asz:X})", derived["cpu_app"])

    def test_dev_id(self):
        """芯片身份（ADR-021）：chip.json 的 device.dev_id == board_config 的 BL_CHIP_DEVID。

        该宏同时用于 GET_INFO / GET_META 上报与参数区记录（0x25-0x26），两侧不一致会让
        主机侧把设备对到错误的芯片档案上。DEV_ID 只到型号系列粒度（F4 的 0x413 覆盖
        F405/407/415/417 各容量），所以唯一性按 (dev_id, Flash 容量) 判定。"""
        seen = {}
        for chip, _ in self.data.values():
            with self.subTest(chip=chip["id"]):
                dev = chip.get("device") or {}
                raw = dev.get("dev_id")
                self.assertIsNotNone(raw, "device.dev_id 缺失（ADR-021）")
                want = int(str(raw), 16)
                self.assertEqual(want & ~0xFFF, 0, f"dev_id 超出 DEV_ID[11:0]：{raw}")
                self.assertEqual(self.val(chip["id"], "BL_CHIP_DEVID"), want)
                key = (want, self.hval(chip["memory"]["flash_size"]))
                self.assertNotIn(key, seen,
                                 f"(dev_id, Flash 容量) 与 {seen.get(key)} 撞车：{raw}")

    def test_copy_units_independent(self):
        """双副本必须各落在独立的擦除单元内（掉电安全语义的前提，ADR-005/015）。"""
        for chip, _ in self.data.values():
            with self.subTest(chip=chip["id"]):
                cid = chip["id"]
                part = chip["partitions"]
                copy_size = self.hval(part["params"]["copies"][0]["size"])
                base = self.hval(part["params"]["base"])
                units = expand_erase_units(chip["erase_units"])
                home = []
                for cbase in (base, base + copy_size):
                    hit = [i for i, (a, s) in enumerate(units) if a <= cbase and cbase + copy_size <= a + s]
                    self.assertEqual(len(hit), 1,
                                     f"参数副本 0x{cbase:08X} 未完整落在单个擦除单元内")
                    home.append(hit[0])
                self.assertNotEqual(home[0], home[1], "双副本必须位于不同擦除单元")

    def test_pins(self):
        """chip.json pins ↔ board_config 板级引脚声明一致（ADR-018）。

        校验 GPIO 管理的引脚与升级串口引脚（led/bt_state/bt_en/i2c_*/uart_tx/
        uart_rx）；uart2_* 属蓝牙端口实现记录，不做宏级校验。清单里没写的键
        （如 f411ceu6 无 bt_en/i2c_*）跳过。"""
        checks = [
            ("uart_tx", "BL_UART_TX", False),
            ("uart_rx", "BL_UART_RX", False),
            ("led", "BL_PIN_LED", True),
            ("bt_state", "BL_PIN_BT_STATE", False),
            ("bt_en", "BL_PIN_BT_EN", False),
            ("i2c_scl", "BL_PIN_I2C_SCL", False),
            ("i2c_sda", "BL_PIN_I2C_SDA", False),
        ]
        for chip, _ in self.data.values():
            with self.subTest(chip=chip["id"]):
                cid = chip["id"]
                pins = chip["pins"]
                for key, macro_base, check_polarity in checks:
                    if key not in pins:
                        continue
                    port, num = parse_pin(pins[key])
                    self.assertEqual(self.val(cid, f"{macro_base}_PORT"), port,
                                     f"{key} 端口序号与 {macro_base}_PORT 不一致")
                    self.assertEqual(self.val(cid, f"{macro_base}_NUM"), num,
                                     f"{key} 引脚号与 {macro_base}_NUM 不一致")
                    if check_polarity and "led_active_low" in pins:
                        self.assertEqual(self.val(cid, "BL_PIN_LED_ACTIVE_LOW"),
                                         1 if pins["led_active_low"] else 0,
                                         "led_active_low 与 BL_PIN_LED_ACTIVE_LOW 不一致")


if __name__ == "__main__":
    unittest.main(verbosity=2)
