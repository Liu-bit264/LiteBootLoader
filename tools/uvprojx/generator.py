#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Keil MDK .uvprojx 生成器：由 JSON 规格（parser.py 输出格式）创建或更新工程文件。

两种模式：
  1) 创建模式（默认）：`generator.py spec.json -o new.uvprojx`
     按内置模板生成全新 .uvprojx；模板元素与顺序对齐真实 uVision5 工程
     （含 xsi 命名空间声明、TargetName/Toolset*/uAC6 直挂 Target、RTE 节点）。
     设备相关字段（FlashDriverDll/RegisterFile/SFDFile/CpuType/调试 DLL 参数）
     一律取自规格 device 字段（ADR-015：由 chipfill.py 从 chips/<id>.json 填充；
     可用 `python tools/uvprojx/chipfill.py --chip chips/<id>.json --target ...` 生成规格），
     未提供时 FlashDriverDll/RegisterFile/SFDFile 留空，需在 Keil 中配置。
  2) 更新模式：`generator.py spec.json --update old.uvprojx`
     只改写规格中出现的字段，其余节点（含未知字段与命名空间声明）原样保留。
     规格来自 parser.py 输出时，近似完成一次"解析-回写"。

两种模式写入前都会自动备份目标文件（--no-backup 关闭），写入前会用
parser 对生成内容做自校验，完成后提示需在 Keil uVision 中人工验证。

退出码：0 成功；1 生成/校验失败；2 参数错误。
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import re
import shutil
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path

HEADER_TEXT = "### uVision Project, (C) Keil Software"
XSI_NAMESPACE = "http://www.w3.org/2001/XMLSchema-instance"
XML_DECLARATION = '<?xml version="1.0" encoding="UTF-8" standalone="no" ?>'

# 与本机安装一致的 AC5 编译器版本记录（toolset.uac6=false 时使用；取自 Keil GUI 保存值）
PCC_USED_AC5 = "5060960::V5.06 update 7 (build 960)::.\\ARMCC"

_CPU_MEM_RE = re.compile(
    r"(IRAM|IROM|XRAM)\(\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\)")

# Cpu 字符串中未给出内存区时的回退值（F103C8）
_DEFAULT_MEMORY = {
    "IRAM": ("0x20000000", "0x5000"),
    "IROM": ("0x08000000", "0x10000"),
}


class UvprojxGenerateError(Exception):
    """规格不合法或生成失败。"""


def _sub(parent, tag, text=None):
    elem = ET.SubElement(parent, tag)
    if text is not None:
        elem.text = str(text)
    return elem


def _flag(parent, tag, value):
    return _sub(parent, tag, 1 if value else 0)


def _parse_cpu_memory(cpu: str) -> dict:
    """从 Cpu 字符串提取 IRAM/IROM/XRAM(起始,大小)；缺失时回退 F103C8 默认值。"""
    found = {m.group(1): (m.group(2), m.group(3))
             for m in _CPU_MEM_RE.finditer(cpu or "")}
    return found or dict(_DEFAULT_MEMORY)


def validate_spec(spec) -> None:
    if not isinstance(spec, dict):
        raise UvprojxGenerateError("规格必须是 JSON 对象")
    targets = spec.get("targets")
    if not isinstance(targets, list) or not targets:
        raise UvprojxGenerateError("规格缺少非空 targets 数组")
    for index, target in enumerate(targets):
        if not isinstance(target, dict):
            raise UvprojxGenerateError(f"targets[{index}] 必须是对象")
        if not str(target.get("name", "")).strip():
            raise UvprojxGenerateError(
                f"targets[{index}] 缺少 name（更新模式用于匹配，创建模式作为 TargetName）")
        for gi, group in enumerate(target.get("groups") or []):
            if not isinstance(group, dict) or not str(group.get("name", "")).strip():
                raise UvprojxGenerateError(f"targets[{index}].groups[{gi}] 缺少 name")
            for fi, file in enumerate(group.get("files") or []):
                if not str(file.get("path", "")).strip():
                    raise UvprojxGenerateError(
                        f"targets[{index}].groups[{gi}].files[{fi}] 缺少 path")


def _build_groups_element(spec_target) -> ET.Element:
    groups = ET.Element("Groups")
    for group in spec_target.get("groups") or []:
        g = _sub(groups, "Group")
        _sub(g, "GroupName", group.get("name", ""))
        files_elem = _sub(g, "Files")
        for file in group.get("files") or []:
            fe = _sub(files_elem, "File")
            _sub(fe, "FileName", file.get("name", ""))
            _sub(fe, "FileType", file.get("type", 1))
            _sub(fe, "FilePath", file.get("path", ""))
    return groups


def _build_arm_ads_misc(spec_target, memory: dict) -> ET.Element:
    misc = ET.Element("ArmAdsMisc")
    for tag, value in (
            ("GenerateListings", 0), ("asHll", 1), ("asAsm", 1), ("asMacX", 1),
            ("asSyms", 1), ("asFals", 1), ("asDbgD", 1), ("asForm", 1),
            ("ldLst", 0), ("ldmm", 1), ("ldXref", 1), ("BigEnd", 0),
            ("AdsALst", 1), ("AdsACrf", 1), ("AdsANop", 0), ("AdsANot", 0),
            ("AdsLLst", 1), ("AdsLmap", 1), ("AdsLcgr", 1), ("AdsLsym", 1),
            ("AdsLszi", 1), ("AdsLtoi", 1), ("AdsLsun", 1), ("AdsLven", 1),
            ("AdsLsxf", 1), ("RvctClst", 0), ("GenPPlst", 0)):
        _sub(misc, tag, value)
    cpu_core = (spec_target.get("device") or {}).get("cpu_core", "Cortex-M3")
    _sub(misc, "AdsCpuType", f'"{cpu_core}"')
    _sub(misc, "RvctDeviceName", "")
    _sub(misc, "mOS", 0)
    _sub(misc, "uocRom", 0)
    _sub(misc, "uocRam", 0)
    iram = memory.get("IRAM")
    irom = memory.get("IROM")
    _flag(misc, "hadIROM", irom is not None)
    _flag(misc, "hadIRAM", iram is not None)
    _flag(misc, "hadXRAM", memory.get("XRAM") is not None)
    _sub(misc, "uocXRam", 0)
    _sub(misc, "RvdsVP", 0)
    _sub(misc, "hadIRAM2", 0)
    _sub(misc, "hadIROM2", 0)
    _sub(misc, "StupSel", 8)
    _flag(misc, "useUlib", bool(spec_target.get("use_microlib", False)))
    _sub(misc, "EndSel", 0)
    _sub(misc, "uLtcg", 0)
    _sub(misc, "nSecure", 0)
    _sub(misc, "RoSelD", 3)
    _sub(misc, "RwSelD", 3)
    _sub(misc, "CodeSel", 0)
    _sub(misc, "OptFeed", 0)
    for tag in ("NoZi1", "NoZi2", "NoZi3", "NoZi4", "NoZi5",
                "Ro1Chk", "Ro2Chk", "Ro3Chk"):
        _sub(misc, tag, 0)
    _sub(misc, "Ir1Chk", 1)
    _sub(misc, "Ir2Chk", 0)
    for tag in ("Ra1Chk", "Ra2Chk", "Ra3Chk"):
        _sub(misc, tag, 0)
    _sub(misc, "Im1Chk", 1)
    _sub(misc, "Im2Chk", 0)

    ocm = _sub(misc, "OnChipMemories")

    def zero_node(tag, node_type):
        node = _sub(ocm, tag)
        _sub(node, "Type", node_type)
        _sub(node, "StartAddress", "0x0")
        _sub(node, "Size", "0x0")
        return node

    def mem_node(tag, mem, node_type):
        node = _sub(ocm, tag)
        _sub(node, "Type", node_type)
        if mem:
            _sub(node, "StartAddress", mem[0])
            _sub(node, "Size", mem[1])
        else:
            _sub(node, "StartAddress", "0x0")
            _sub(node, "Size", "0x0")

    for tag in ("Ocm1", "Ocm2", "Ocm3", "Ocm4", "Ocm5", "Ocm6"):
        zero_node(tag, 0)
    mem_node("IRAM", iram, 0)
    mem_node("IROM", irom, 1)
    mem_node("XRAM", memory.get("XRAM"), 0)
    for tag in ("OCR_RVCT1", "OCR_RVCT2", "OCR_RVCT3"):
        zero_node(tag, 1)
    mem_node("OCR_RVCT4", irom, 1)   # 默认 CODE 区跟随 IROM
    zero_node("OCR_RVCT5", 1)
    for tag in ("OCR_RVCT6", "OCR_RVCT7", "OCR_RVCT8"):
        zero_node(tag, 0)
    mem_node("OCR_RVCT9", iram, 0)   # 默认 RAM 区跟随 IRAM
    zero_node("OCR_RVCT10", 0)
    _sub(misc, "RvctStartVector", "")
    return misc


def _build_cads(spec_target) -> ET.Element:
    cads = ET.Element("Cads")
    for tag, value in (
            ("interw", 1), ("Optim", 1), ("oTime", 0), ("SplitLS", 0),
            ("OneElfS", 1), ("Strict", 0), ("EnumInt", 0), ("PlainCh", 0),
            ("Ropi", 0), ("Rwpi", 0), ("wLevel", 2), ("uThumb", 0),
            ("uSurpInc", 0), ("uC99", 1), ("useXO", 0), ("v6Lang", 5),
            ("v6LangP", 1), ("vShortEn", 1), ("vShortWch", 1),
            ("v6Lto", 0), ("v6WtE", 0), ("v6Rtti", 0)):
        _sub(cads, tag, value)
    vc = _sub(cads, "VariousControls")
    _sub(vc, "MiscControls", spec_target.get("c_misc_controls", ""))
    _sub(vc, "Define", ",".join(spec_target.get("c_defines") or []))
    _sub(vc, "Undefine", "")
    _sub(vc, "IncludePath", ";".join(spec_target.get("c_include_paths") or []))
    _sub(cads, "ScatterFile", spec_target.get("scatter_file", ""))
    return cads


def _build_aads() -> ET.Element:
    aads = ET.Element("Aads")
    for tag, value in (
            ("interw", 1), ("Ropi", 0), ("Rwpi", 0), ("thumb", 0),
            ("SplitLS", 0), ("SwStkChk", 0), ("NoWarn", 0),
            ("uSurpInc", 0), ("useXO", 0), ("uClangAs", 0)):
        _sub(aads, tag, value)
    vc = _sub(aads, "VariousControls")
    for tag in ("MiscControls", "Define", "Undefine", "IncludePath"):
        _sub(vc, tag, "")
    return aads


def _build_ldads(spec_target, memory: dict) -> ET.Element:
    scatter = spec_target.get("scatter_file", "")
    ldads = ET.Element("LDads")
    # 指定 scatter 时关闭"使用 Target 对话框内存布局"，改用 scatter 文件
    _sub(ldads, "umfTarg", 0 if scatter else 1)
    _sub(ldads, "Ropi", 0)
    _sub(ldads, "Rwpi", 0)
    _sub(ldads, "noStLib", 0)
    _sub(ldads, "RepFail", 1)
    _sub(ldads, "useFile", 1 if scatter else 0)
    iram = memory.get("IRAM")
    irom = memory.get("IROM")
    _sub(ldads, "TextAddressRange", irom[0] if irom else "0x08000000")
    _sub(ldads, "DataAddressRange", iram[0] if iram else "0x20000000")
    _sub(ldads, "pXoBase", "")
    _sub(ldads, "ScatterFile", spec_target.get("linker_scatter_file", "") or scatter)
    for tag in ("IncludeLibs", "IncludeLibsPath", "Misc", "LinkerInputFile",
                "DisabledWarnings"):
        _sub(ldads, tag, "")
    return ldads


def _build_target_option(spec_target) -> ET.Element:
    device = spec_target.get("device") or {}
    device_name = device.get("name", "")
    memory = _parse_cpu_memory(device.get("cpu", ""))

    opt = ET.Element("TargetOption")
    common = _sub(opt, "TargetCommonOption")
    _sub(common, "Device", device_name)
    _sub(common, "Vendor", device.get("vendor", ""))
    _sub(common, "PackID", device.get("pack_id", ""))
    _sub(common, "PackURL", "http://www.keil.com/pack/")
    _sub(common, "Cpu", device.get("cpu", ""))
    for tag in ("FlashUtilSpec", "StartupFile"):
        _sub(common, tag, "")
    _sub(common, "FlashDriverDll", device.get("flash_driver", ""))
    _sub(common, "RegisterFile", device.get("register_file", ""))
    _sub(common, "DeviceId", 0)
    for tag in ("MemoryEnv", "Cmp", "Asm", "Linker", "OHString",
                "InfinionOptionDll", "SLE66CMisc", "SLE66AMisc", "SLE66LinkerMisc"):
        _sub(common, tag, "")
    _sub(common, "SFDFile", device.get("sfd_file", ""))
    _sub(common, "bCustSvd", 0)
    _sub(common, "UseEnv", 0)
    for tag in ("BinPath", "IncludePath", "LibPath", "RegisterFilePath",
                "DBRegisterFilePath"):
        _sub(common, tag, "")
    status = _sub(common, "TargetStatus")
    for tag in ("Error", "ExitCodeStop", "ButtonStop", "NotGenerated", "InvalidFlash"):
        _sub(status, tag, 0)
    output = spec_target.get("output") or {}
    _sub(common, "OutputDirectory", output.get("directory", ".\\Objects\\"))
    _sub(common, "OutputName", output.get("name", ""))
    _flag(common, "CreateExecutable", output.get("create_executable", True))
    _sub(common, "CreateLib", 0)
    _flag(common, "CreateHexFile", output.get("create_hex_file", False))
    _sub(common, "DebugInformation", 1)
    _sub(common, "BrowseInformation", 1)
    _sub(common, "ListingPath", ".\\Listings\\")
    _sub(common, "HexFormatSelection", 1)
    _sub(common, "Merge32K", 0)
    _sub(common, "CreateBatchFile", 0)

    stop_tags = {
        "BeforeCompile": ("UserProg1Dos16Mode", "UserProg2Dos16Mode", "nStopU1X", "nStopU2X"),
        "BeforeMake": ("nStopB1X", "nStopB2X"),
        "AfterMake": ("nStopA1X", "nStopA2X"),
    }
    for section, extra in stop_tags.items():
        node = _sub(common, section)
        _sub(node, "RunUserProg1", 0)
        _sub(node, "RunUserProg2", 0)
        _sub(node, "UserProg1Name", "")
        _sub(node, "UserProg2Name", "")
        for tag in extra:
            _sub(node, tag, 0)
    _sub(common, "SelectedForBatchBuild", 0)
    _sub(common, "SVCSIdString", "")

    prop = _sub(opt, "CommonProperty")
    for tag, value in (
            ("UseCPPCompiler", 0), ("RVCTCodeConst", 0), ("RVCTZI", 0),
            ("RVCTOtherData", 0), ("ModuleSelection", 0), ("IncludeInBuild", 1),
            ("AlwaysBuild", 0), ("GenerateAssemblyFile", 0),
            ("AssembleAssemblyFile", 0), ("PublicsOnly", 0), ("StopOnExitCode", 3)):
        _sub(prop, tag, value)
    _sub(prop, "CustomArgument", "")
    _sub(prop, "IncludeLibraryModules", "")
    _sub(prop, "ComprImg", 1)

    dll = _sub(opt, "DllOption")
    debug_args = device.get("debug_dll_args", "-pCM3")
    _sub(dll, "SimDllName", "SARMCM3.DLL")
    _sub(dll, "SimDllArguments", " -REMAP")
    _sub(dll, "SimDlgDll", "DCM.DLL")
    _sub(dll, "SimDlgDllArguments", debug_args)
    _sub(dll, "TargetDllName", "SARMCM3.DLL")
    _sub(dll, "TargetDllArguments", "")
    _sub(dll, "TargetDlgDll", "TCM.DLL")
    _sub(dll, "TargetDlgDllArguments", debug_args)

    debug = _sub(opt, "DebugOption")
    opthx = _sub(debug, "OPTHX")
    _sub(opthx, "HexSelection", 1)
    _sub(opthx, "HexRangeLowAddress", 0)
    _sub(opthx, "HexRangeHighAddress", 0)
    _sub(opthx, "HexOffset", 0)
    _sub(opthx, "Oh166RecLen", 16)

    util = _sub(opt, "Utilities")
    flash1 = _sub(util, "Flash1")
    _sub(flash1, "UseTargetDll", 1)
    _sub(flash1, "UseExternalTool", 0)
    _sub(flash1, "RunIndependent", 0)
    _sub(flash1, "UpdateFlashBeforeDebugging", 1)
    _sub(flash1, "Capability", 0)
    _sub(flash1, "DriverSelection", -1)
    _sub(util, "bUseTDR", 1)
    _sub(util, "Flash2", "BIN\\UL2CM3.DLL")
    for tag in ("Flash3", "Flash4", "pFcarmOut", "pFcarmGrp", "pFcArmRoot"):
        _sub(util, tag, "")
    _sub(util, "FcArmLst", 0)

    ads = _sub(opt, "TargetArmAds")
    ads.append(_build_arm_ads_misc(spec_target, memory))
    ads.append(_build_cads(spec_target))
    ads.append(_build_aads())
    ads.append(_build_ldads(spec_target, memory))
    return opt


def build_project_tree(spec: dict) -> ET.Element:
    root = ET.Element("Project")
    root.set("xmlns:xsi", XSI_NAMESPACE)
    root.set("xsi:noNamespaceSchemaLocation", "project_projx.xsd")
    _sub(root, "SchemaVersion", spec.get("schema_version") or "2.1")
    _sub(root, "Header", HEADER_TEXT)
    targets = _sub(root, "Targets")
    for spec_target in spec["targets"]:
        toolset = spec_target.get("toolset") or {}
        target = _sub(targets, "Target")
        _sub(target, "TargetName", spec_target["name"])
        _sub(target, "ToolsetNumber", toolset.get("number") or "0x4")
        _sub(target, "ToolsetName", toolset.get("name") or "ARM-ADS")
        uac6 = bool(toolset.get("uac6", True))
        if not uac6:
            _sub(target, "pCCUsed", toolset.get("pcc_used") or PCC_USED_AC5)
        _flag(target, "uAC6", uac6)
        target.append(_build_target_option(spec_target))
        target.append(_build_groups_element(spec_target))
    rte = _sub(root, "RTE")
    for tag in ("apis", "components", "files"):
        _sub(rte, tag)
    return root


def _set_child_text(parent, tag, value) -> None:
    child = parent.find(tag)
    if child is None:
        child = ET.SubElement(parent, tag)
    child.text = str(value)


def apply_spec_to_target(target, spec_target) -> None:
    """更新模式：只写规格中出现的字段，未提及的节点保持原样。"""
    common = target.find("TargetOption/TargetCommonOption")
    if common is None:
        raise UvprojxGenerateError("目标缺少 TargetOption/TargetCommonOption，无法更新")

    device = spec_target.get("device") or {}
    for key, tag in (("name", "Device"), ("vendor", "Vendor"),
                     ("pack_id", "PackID"), ("cpu", "Cpu")):
        if key in device:
            _set_child_text(common, tag, device[key])

    output = spec_target.get("output") or {}
    if "directory" in output:
        _set_child_text(common, "OutputDirectory", output["directory"])
    if "name" in output:
        _set_child_text(common, "OutputName", output["name"])
    if "create_executable" in output:
        _set_child_text(common, "CreateExecutable",
                        1 if output["create_executable"] else 0)
    if "create_hex_file" in output:
        _set_child_text(common, "CreateHexFile",
                        1 if output["create_hex_file"] else 0)

    toolset = spec_target.get("toolset") or {}
    if "uac6" in toolset:
        _set_child_text(target, "uAC6", 1 if toolset["uac6"] else 0)
    if "pcc_used" in toolset:
        _set_child_text(target, "pCCUsed", toolset["pcc_used"])

    cads_keys = ("c_defines", "c_include_paths", "c_misc_controls", "scatter_file")
    if any(key in spec_target for key in cads_keys):
        cads = target.find("TargetOption/TargetArmAds/Cads")
        if cads is None:
            raise UvprojxGenerateError("目标缺少 TargetArmAds/Cads 节点，无法更新编译设置")
        vc = cads.find("VariousControls")
        if vc is None:
            vc = ET.SubElement(cads, "VariousControls")
        if "c_defines" in spec_target:
            _set_child_text(vc, "Define", ",".join(spec_target["c_defines"]))
        if "c_include_paths" in spec_target:
            _set_child_text(vc, "IncludePath", ";".join(spec_target["c_include_paths"]))
        if "c_misc_controls" in spec_target:
            _set_child_text(vc, "MiscControls", spec_target["c_misc_controls"])
        if "scatter_file" in spec_target:
            _set_child_text(cads, "ScatterFile", spec_target["scatter_file"])

    if "linker_scatter_file" in spec_target:
        ldads = target.find("TargetOption/TargetArmAds/LDads")
        if ldads is not None:
            _set_child_text(ldads, "ScatterFile", spec_target["linker_scatter_file"])

    if "groups" in spec_target:
        old_groups = target.find("Groups")
        new_groups = _build_groups_element(spec_target)
        if old_groups is not None:
            index = list(target).index(old_groups)
            target.remove(old_groups)
            target.insert(index, new_groups)
        else:
            target.append(new_groups)

    if "use_microlib" in spec_target:
        misc = target.find("TargetOption/TargetArmAds/ArmAdsMisc")
        if misc is not None:
            _set_child_text(misc, "useUlib", 1 if spec_target["use_microlib"] else 0)


def _load_parser_module():
    parser_path = Path(__file__).with_name("parser.py")
    spec_obj = importlib.util.spec_from_file_location("bl_uvprojx_parser", parser_path)
    module = importlib.util.module_from_spec(spec_obj)
    spec_obj.loader.exec_module(module)
    return module


def serialize_tree(tree: ET.ElementTree) -> bytes:
    try:
        ET.indent(tree, space="  ")
    except AttributeError:  # Python < 3.9 无 ET.indent
        pass
    body = ET.tostring(tree.getroot(), encoding="unicode")
    return (XML_DECLARATION + "\r\n" + body.replace("\n", "\r\n") + "\r\n").encode("utf-8")


def write_tree(tree: ET.ElementTree, dest: Path, backup: bool):
    data = serialize_tree(tree)
    parser_mod = _load_parser_module()
    try:
        check = parser_mod.parse_bytes(data)
    except parser_mod.UvprojxParseError as exc:
        raise UvprojxGenerateError(f"生成内容自校验失败：{exc}") from exc
    if not check.get("targets"):
        raise UvprojxGenerateError("生成内容自校验失败：没有 target")

    dest = Path(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    backup_path = None
    if dest.exists() and backup:
        stamp = time.strftime("%Y%m%d-%H%M%S")
        backup_path = str(dest.with_name(dest.name + f".bak-{stamp}"))
        shutil.copy2(dest, backup_path)
    dest.write_bytes(data)
    return backup_path


def create_project_file(spec, dest, backup: bool = True):
    validate_spec(spec)
    return write_tree(ET.ElementTree(build_project_tree(spec)), Path(dest), backup)


def update_project_file(path, spec, backup: bool = True):
    validate_spec(spec)
    path = Path(path)
    if not path.is_file():
        raise UvprojxGenerateError(f"要更新的工程不存在: {path}")
    try:
        tree = ET.parse(path)
    except ET.ParseError as exc:
        raise UvprojxGenerateError(f"现有工程 XML 解析失败: {exc}") from exc
    root = tree.getroot()
    if root.tag != "Project":
        raise UvprojxGenerateError(f"根元素应为 <Project>，实际 <{root.tag}>")

    existing = {}
    for t in root.findall("Targets/Target"):
        name = (t.findtext("TargetName") or "").strip() or \
               (t.findtext("TargetOption/TargetCommonOption/TargetName") or "").strip()
        existing[name] = t
    for spec_target in spec["targets"]:
        node = existing.get(spec_target["name"])
        if node is None:
            raise UvprojxGenerateError(
                f"现有工程中没有名为 '{spec_target['name']}' 的 target"
                f"（现有: {', '.join(existing) or '无'}）")
        apply_spec_to_target(node, spec_target)
    return write_tree(tree, path, backup)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="由 JSON 规格创建或更新 Keil .uvprojx")
    ap.add_argument("spec", help="规格 JSON 文件（parser.py 输出格式）")
    ap.add_argument("-o", "--output", help="创建模式：要生成的 .uvprojx 路径")
    ap.add_argument("--update", metavar="EXISTING",
                    help="更新模式：在现有 .uvprojx 上只应用规格字段")
    ap.add_argument("--no-backup", action="store_true", help="覆盖前不备份")
    args = ap.parse_args(argv)

    if bool(args.output) == bool(args.update):
        ap.error("必须且只能选择 -o <输出路径> 或 --update <现有工程> 之一")

    try:
        spec = json.loads(Path(args.spec).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"[generator] 读取规格失败: {exc}", file=sys.stderr)
        return 1

    try:
        if args.output:
            backup_path = create_project_file(spec, args.output,
                                              backup=not args.no_backup)
            action = f"已生成 {args.output}"
        else:
            backup_path = update_project_file(args.update, spec,
                                              backup=not args.no_backup)
            action = f"已更新 {args.update}"
    except (UvprojxGenerateError, ET.ParseError) as exc:
        print(f"[generator] 错误: {exc}", file=sys.stderr)
        return 1

    print(f"[generator] {action}"
          + (f"（原文件已备份为 {backup_path}）" if backup_path else ""))
    print("[提示] 生成结果需在 Keil uVision 中打开并人工验证："
          "Device/Pack、Include Paths、Define、Scatter 与源文件分组，确认后再编译。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
