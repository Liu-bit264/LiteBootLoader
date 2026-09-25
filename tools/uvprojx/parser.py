#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Keil MDK .uvprojx 解析器：把工程文件解析为 JSON。

提取内容：
  - target 名称、工具链信息（ToolsetNumber/ToolsetName/pCCUsed/uAC6）
  - device（Device/Vendor/PackID/Cpu）
  - 输出设置（目录/名称/生成可执行/生成 HEX）
  - C 编译器宏定义、include 路径、MiscControls
  - scatter file（Cads 与 LDads 两处分别记录）
  - 源文件分组（组名、文件名、类型、路径）

安全约束：解析前拒绝含 DTD（<!DOCTYPE / <!ENTITY）的 XML 并限制输入大小，
防范 XML 实体扩展导致的资源耗尽（标准库 ElementTree 不做防护）。

用法：
    python parser.py <input.uvprojx>                # 解析结果打印到 stdout
    python parser.py <input.uvprojx> -o spec.json   # 写入 JSON 文件
    python parser.py <input.uvprojx> --compact      # 单行 JSON

输出 JSON 即 generator.py 接受的规格格式，两者构成"解析-回写"闭环。

退出码：0 成功；1 读取/解析失败；2 参数错误。
"""

from __future__ import annotations

import argparse
import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

SCHEMA = "bl-uvprojx-parse/1"

# 真实 .uvprojx 远小于该上限；超过即视为异常输入
_MAX_INPUT_BYTES = 32 * 1024 * 1024

FILE_TYPE_NAMES = {
    1: "C",
    2: "ASM",
    3: "OBJECT",
    4: "LIB",
    5: "TEXT",
    7: "OTHER",
    8: "C++",
}


class UvprojxParseError(Exception):
    """uvprojx 读取或结构错误。"""


def _check_no_dtd(data: bytes) -> None:
    lowered = data.lower()
    for marker in (b"<!doctype", b"<!entity"):
        pos = lowered.find(marker)
        if pos != -1:
            raise UvprojxParseError(
                f"检测到 {marker.decode()} 声明（偏移 {pos}）："
                "为防范 XML 实体扩展攻击，拒绝解析含 DTD 的 XML")


def _text(elem, default: str = "") -> str:
    if elem is not None and elem.text is not None:
        value = elem.text.strip()
        if value:
            return value
    return default


def _split_multi(raw: str) -> list:
    """按 ';' 或 ',' 拆分为列表（uvprojx 的 Define/IncludePath 约定分隔符）。

    限制：路径本身含 ',' 的场景不受支持（Keil 工程中极罕见）。
    """
    return [item.strip() for item in raw.replace(";", ",").split(",") if item.strip()]


def _parse_file(file_elem) -> dict:
    name = _text(file_elem.find("FileName"))
    type_raw = _text(file_elem.find("FileType"))
    path = _text(file_elem.find("FilePath"))
    try:
        ftype = int(type_raw)
    except ValueError:
        ftype = -1
    return {
        "name": name,
        "type": ftype,
        "type_name": FILE_TYPE_NAMES.get(ftype, "UNKNOWN"),
        "path": path,
    }


def _parse_group(group_elem) -> dict:
    files = [_parse_file(f) for f in group_elem.findall("Files/File")]
    return {"name": _text(group_elem.find("GroupName")), "files": files}


def _parse_target(target_elem) -> dict:
    common = target_elem.find("TargetOption/TargetCommonOption")
    if common is None:
        raise UvprojxParseError("缺少 TargetOption/TargetCommonOption 节点")
    cads = target_elem.find("TargetOption/TargetArmAds/Cads")
    ldads = target_elem.find("TargetOption/TargetArmAds/LDads")

    # uVision5 中 TargetName 是 Target 的直接子节点；兼容老格式的 TargetCommonOption 内写法
    name = (target_elem.findtext("TargetName") or "").strip()
    if not name:
        name = _text(common.find("TargetName"))

    uac6_raw = (target_elem.findtext("uAC6") or "").strip()
    toolset = {
        "number": (target_elem.findtext("ToolsetNumber") or "").strip(),
        "name": (target_elem.findtext("ToolsetName") or "").strip(),
        "pcc_used": (target_elem.findtext("pCCUsed") or "").strip(),
        "uac6": uac6_raw == "1",
    }

    device = {
        "name": _text(common.find("Device")),
        "vendor": _text(common.find("Vendor")),
        "pack_id": _text(common.find("PackID")),
        "cpu": _text(common.find("Cpu")),
    }
    output = {
        "directory": _text(common.find("OutputDirectory")),
        "name": _text(common.find("OutputName")),
        "create_executable": _text(common.find("CreateExecutable")) == "1",
        "create_hex_file": _text(common.find("CreateHexFile")) == "1",
    }

    defines = []
    include_paths = []
    misc_controls = ""
    scatter_file = ""
    if cads is not None:
        vc = cads.find("VariousControls")
        if vc is not None:
            defines = _split_multi(_text(vc.find("Define")))
            include_paths = _split_multi(_text(vc.find("IncludePath")))
            misc_controls = _text(vc.find("MiscControls"))
        scatter_file = _text(cads.find("ScatterFile"))
    linker_scatter_file = _text(ldads.find("ScatterFile")) if ldads is not None else ""

    groups = [_parse_group(g) for g in target_elem.findall("Groups/Group")]

    return {
        "name": name,
        "toolset": toolset,
        "device": device,
        "output": output,
        "c_defines": defines,
        "c_include_paths": include_paths,
        "c_misc_controls": misc_controls,
        "scatter_file": scatter_file,
        "linker_scatter_file": linker_scatter_file,
        "groups": groups,
    }


def parse_bytes(data: bytes, source: str = "<memory>") -> dict:
    """从字节流解析 uvprojx 内容（供生成器写入前自校验复用）。"""
    if len(data) > _MAX_INPUT_BYTES:
        raise UvprojxParseError(
            f"输入过大（{len(data)} 字节 > 上限 {_MAX_INPUT_BYTES}），拒绝解析")
    _check_no_dtd(data)
    try:
        root = ET.fromstring(data)
    except ET.ParseError as exc:
        raise UvprojxParseError(f"XML 解析失败: {exc}") from exc
    if root.tag != "Project":
        raise UvprojxParseError(
            f"根元素应为 <Project>，实际为 <{root.tag}>，可能不是 .uvprojx 文件")

    targets = [_parse_target(t) for t in root.findall("Targets/Target")]
    if not targets:
        raise UvprojxParseError("未找到任何 <Targets/Target> 节点")

    return {
        "schema": SCHEMA,
        "source": source,
        "schema_version": (root.findtext("SchemaVersion") or "").strip(),
        "header": (root.findtext("Header") or "").strip(),
        "targets": targets,
    }


def parse_uvprojx(path) -> dict:
    path = Path(path)
    if not path.is_file():
        raise UvprojxParseError(f"文件不存在: {path}")
    return parse_bytes(path.read_bytes(), str(path))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="解析 Keil .uvprojx 为 JSON（generator.py 的规格格式）")
    ap.add_argument("input", help="输入 .uvprojx 文件")
    ap.add_argument("-o", "--output", help="输出 JSON 文件（默认打印到 stdout）")
    ap.add_argument("--compact", action="store_true", help="输出单行 JSON")
    args = ap.parse_args(argv)

    try:
        data = parse_uvprojx(args.input)
    except UvprojxParseError as exc:
        print(f"[parser] 错误: {exc}", file=sys.stderr)
        return 1

    text = json.dumps(data, ensure_ascii=False, indent=None if args.compact else 2)
    if args.output:
        Path(args.output).write_text(text + "\n", encoding="utf-8")
        print(f"[parser] 已写入 {args.output}")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
