#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CSP 芯片清单填充器（ADR-015）：chips/<id>.json + 模板 → per-chip 的 .spec.json 与 .sct。

"加芯片 = 建目录 + chip.json + 实现 port ops，构建零手工"（porting_guide.md §CSP）：
构建脚本以本工具产出的 .spec.json 作为 generator.py 的输入，工程文件因此完全可再生成。

用法：
  python tools/uvprojx/chipfill.py --chip chips/f103c8t6.json --target bootloader \
      --spec-out bootloader.spec.json --sct-out linker/bootloader.sct
  --target 取 bootloader|app；--spec-out / --sct-out 省略时只做校验并打印渲染结果摘要
  （--print-spec 可输出完整 spec JSON）。模板路径可用 --template / --sct-template 覆盖。

占位符语法（模板保持合法 JSON）：
  "{{chip.a.b.c}}"    整值引用：整个字符串恰为一个引用时，标量直接替换，
                      列表/对象展开进所在容器（列表拼接、对象内联）；
                      嵌在更长字符串中时按标量做文本替换。
  {"$chip": "a.b.c"}  容器展开：等价于整值引用的列表拼接/对象内联。

派生值：加载芯片清单后自动补 chip["derived"]：
  cpu_bootloader = IRAM(SRAM) IROM(Flash 全区) CPUTYPE(...) CLOCK(...) ELITTLE
  cpu_app        = IRAM(SRAM) IROM(APP 分区) CPUTYPE(...) CLOCK(...) ELITTLE

退出码：0 成功；1 失败（清单/模板缺失、引用未定义等）。
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
TEMPLATE_DIR = Path(__file__).resolve().parent / "templates"
SCT_TEMPLATE_DIR = REPO_ROOT / "linker" / "templates"

_TOKEN_RE = re.compile(r"^\{\{chip\.([A-Za-z0-9_.]+)\}\}$")
_EMBED_RE = re.compile(r"\{\{chip\.([A-Za-z0-9_.]+)\}\}")
_TARGETS = ("bootloader", "app")


class ChipFillError(Exception):
    """清单或模板不合法、引用未定义。"""


def load_chip(path: str | Path) -> dict:
    p = Path(path)
    try:
        chip = json.loads(p.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ChipFillError(f"读取芯片清单失败 {p}: {exc}") from exc
    for key in ("id", "device", "build", "memory", "partitions", "erase_units"):
        if key not in chip:
            raise ChipFillError(f"芯片清单缺少必需键: {key}")
    chip["derived"] = {
        "cpu_bootloader": _compose_cpu(
            chip, chip["memory"]["flash_base"], chip["memory"]["flash_size"]),
        "cpu_app": _compose_cpu(
            chip, chip["partitions"]["app"]["base"], chip["partitions"]["app"]["size"]),
        "id": chip["id"],
    }
    return chip


def _compose_cpu(chip: dict, rom_base: str, rom_size: str) -> str:
    mem = chip["memory"]
    dev = chip["device"]

    # Keil Cpu 字符串惯例：地址固定 8 位十六进制（0x08000000），尺寸去前导零（0x5000）
    addr = lambda v: f"0x{int(v, 16):08X}"
    size = lambda v: f"0x{int(v, 16):X}"

    return (f"IRAM({addr(mem['sram_base'])},{size(mem['sram_size'])}) "
            f"IROM({addr(rom_base)},{size(rom_size)}) "
            f"CPUTYPE(\"{dev['cputype']}\") {dev['cpu_clock']} {dev['endianness']}")


def _lookup(chip: dict, dotted: str):
    node = chip
    for part in dotted.split("."):
        if not isinstance(node, dict) or part not in node:
            raise ChipFillError(f"占位符引用未定义: chip.{dotted}")
        node = node[part]
    return node


def _fill(node, chip: dict):
    if isinstance(node, dict):
        out = {}
        for key, value in node.items():
            if key == "$chip" and isinstance(value, str):
                return _fill(_lookup(chip, value), chip)  # 对象内联（调用方负责拼接）
            out[key] = _fill(value, chip)
        return out
    if isinstance(node, list):
        out = []
        for item in node:
            if isinstance(item, dict) and set(item) == {"$chip"}:
                resolved = _fill(_lookup(chip, item["$chip"]), chip)
                if isinstance(resolved, list):
                    out.extend(resolved)   # 列表拼接
                else:
                    out.append(resolved)
            else:
                out.append(_fill(item, chip))
        return out
    if isinstance(node, str):
        whole = _TOKEN_RE.match(node.strip())
        if whole:
            return _lookup(chip, whole.group(1))
        return _EMBED_RE.sub(lambda m: str(_scalar(chip, m.group(1))), node)
    return node


def _scalar(chip: dict, dotted: str):
    value = _lookup(chip, dotted)
    if isinstance(value, (list, dict)):
        raise ChipFillError(f"chip.{dotted} 是容器，不能嵌入字符串（请用整值引用）")
    return value


def build_spec(chip: dict, target: str, template_path: str | Path | None = None) -> dict:
    if target not in _TARGETS:
        raise ChipFillError(f"未知 target: {target}（可选 {'|'.join(_TARGETS)}）")
    tpl = Path(template_path) if template_path else TEMPLATE_DIR / f"{target}.spec.template.json"
    try:
        template = json.loads(tpl.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ChipFillError(f"读取模板失败 {tpl}: {exc}") from exc
    return _fill(template, chip)


def render_sct(chip: dict, target: str, template_path: str | Path | None = None) -> str:
    if target not in _TARGETS:
        raise ChipFillError(f"未知 target: {target}（可选 {'|'.join(_TARGETS)}）")
    tpl = Path(template_path) if template_path else SCT_TEMPLATE_DIR / f"{target}.sct.template"
    try:
        text = tpl.read_text(encoding="utf-8")
    except OSError as exc:
        raise ChipFillError(f"读取 scatter 模板失败 {tpl}: {exc}") from exc
    return _EMBED_RE.sub(lambda m: str(_scalar(chip, m.group(1))), text)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="由芯片清单与模板生成 .spec.json 与 .sct")
    ap.add_argument("--chip", required=True, help="芯片清单 JSON（chips/<id>.json）")
    ap.add_argument("--target", required=True, choices=_TARGETS)
    ap.add_argument("--spec-out", help="输出 .spec.json 路径（省略则不写出）")
    ap.add_argument("--sct-out", help="输出 .sct 路径（省略则不写出）")
    ap.add_argument("--template", help="覆盖 spec 模板路径")
    ap.add_argument("--sct-template", help="覆盖 sct 模板路径")
    ap.add_argument("--print-spec", action="store_true", help="打印渲染后的 spec JSON")
    args = ap.parse_args(argv)

    try:
        chip = load_chip(args.chip)
        spec = build_spec(chip, args.target, args.template)
        if args.print_spec:
            print(json.dumps(spec, ensure_ascii=False, indent=2))
        if args.spec_out:
            Path(args.spec_out).write_bytes(
                (json.dumps(spec, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
        if args.sct_out:
            Path(args.sct_out).write_bytes(
                render_sct(chip, args.target, args.sct_template).encode("utf-8"))
    except ChipFillError as exc:
        print(f"[chipfill] 错误: {exc}", file=sys.stderr)
        return 1

    done = [name for name, flag in (("spec", args.spec_out), ("sct", args.sct_out)) if flag]
    print(f"[chipfill] {chip['id']}/{args.target}: "
          + ("已生成 " + "、".join(done) if done else "校验通过（未写出文件）"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
