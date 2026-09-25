#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""uvprojx 工具基本测试：直接 `python test_uvprojx.py` 运行（unittest，无外部依赖）。"""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import generator as uv_generator  # noqa: E402
import parser as uv_parser  # noqa: E402

# 结构对齐真实 uVision5 工程：TargetName/Toolset*/uAC6 直挂 Target，
# 根节点带 xsi 命名空间声明，另放一个"未知字段"验证更新模式保留行为。
SAMPLE = r"""<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<Project xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="project_projx.xsd">
  <SchemaVersion>2.1</SchemaVersion>
  <Header>### uVision Project, (C) Keil Software</Header>
  <Targets>
    <Target>
      <TargetName>bootloader</TargetName>
      <ToolsetNumber>0x4</ToolsetNumber>
      <ToolsetName>ARM-ADS</ToolsetName>
      <pCCUsed>5060528::V5.06 update 5 (build 528)::ARMCC</pCCUsed>
      <uAC6>0</uAC6>
      <TargetOption>
        <TargetCommonOption>
          <Device>STM32F103C8</Device>
          <Vendor>STMicroelectronics</Vendor>
          <PackID>Keil.STM32F1xx_DFP.2.2.0</PackID>
          <Cpu>IRAM(0x20000000,0x5000) IROM(0x08000000,0x10000) CPUTYPE("Cortex-M3")</Cpu>
          <OutputDirectory>.\Objects\</OutputDirectory>
          <OutputName>bootloader</OutputName>
          <CreateExecutable>1</CreateExecutable>
          <CreateHexFile>0</CreateHexFile>
        </TargetCommonOption>
        <TargetArmAds>
          <Cads>
            <VariousControls>
              <MiscControls></MiscControls>
              <Define>STM32F10X_MD,USE_STDPERIPH_DRIVER</Define>
              <Undefine></Undefine>
              <IncludePath>..\core;..\port\stm32f1\f103c8t6</IncludePath>
            </VariousControls>
            <ScatterFile>..\linker\bootloader.sct</ScatterFile>
          </Cads>
          <LDads>
            <ScatterFile></ScatterFile>
          </LDads>
        </TargetArmAds>
      </TargetOption>
      <Groups>
        <Group>
          <GroupName>Core</GroupName>
          <Files>
            <File>
              <FileName>bl_core.c</FileName>
              <FileType>1</FileType>
              <FilePath>..\core\bl_core.c</FilePath>
            </File>
          </Files>
        </Group>
        <Group>
          <GroupName>ASM</GroupName>
          <Files>
            <File>
              <FileName>startup_stm32f10x_md.s</FileName>
              <FileType>2</FileType>
              <FilePath>..\port\stm32f1\f103c8t6\startup_stm32f10x_md.s</FilePath>
            </File>
          </Files>
        </Group>
      </Groups>
      <VendorSpecificKeepMe>42</VendorSpecificKeepMe>
    </Target>
  </Targets>
  <RTE>
    <apis/>
    <components/>
    <files/>
  </RTE>
</Project>
"""

COMPARE_KEYS = ("name", "toolset", "device", "output", "c_defines",
                "c_include_paths", "c_misc_controls", "scatter_file",
                "linker_scatter_file", "groups")


class UvprojxToolTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)
        self.sample = self.dir / "sample.uvprojx"
        self.sample.write_bytes(SAMPLE.encode("utf-8"))
        self.spec = uv_parser.parse_uvprojx(self.sample)

    def tearDown(self):
        self._tmp.cleanup()

    def test_parse_fields(self):
        target = self.spec["targets"][0]
        self.assertEqual(target["name"], "bootloader")
        self.assertEqual(target["toolset"]["name"], "ARM-ADS")
        self.assertEqual(target["toolset"]["number"], "0x4")
        self.assertFalse(target["toolset"]["uac6"])
        self.assertEqual(target["device"]["name"], "STM32F103C8")
        self.assertEqual(target["device"]["vendor"], "STMicroelectronics")
        self.assertEqual(target["device"]["pack_id"], "Keil.STM32F1xx_DFP.2.2.0")
        self.assertEqual(target["c_defines"],
                         ["STM32F10X_MD", "USE_STDPERIPH_DRIVER"])
        self.assertEqual(target["c_include_paths"],
                         ["..\\core", "..\\port\\stm32f1\\f103c8t6"])
        self.assertEqual(target["scatter_file"], "..\\linker\\bootloader.sct")
        self.assertEqual(target["linker_scatter_file"], "")
        self.assertEqual(target["output"]["name"], "bootloader")
        self.assertTrue(target["output"]["create_executable"])
        self.assertFalse(target["output"]["create_hex_file"])
        self.assertEqual(target["groups"][0]["name"], "Core")
        self.assertEqual(target["groups"][0]["files"][0]["path"], "..\\core\\bl_core.c")
        self.assertEqual(target["groups"][1]["files"][0]["type"], 2)
        self.assertEqual(target["groups"][1]["files"][0]["type_name"], "ASM")

    def test_roundtrip_parse_generate_parse(self):
        out = self.dir / "roundtrip.uvprojx"
        uv_generator.create_project_file(self.spec, out, backup=False)
        spec2 = uv_parser.parse_uvprojx(out)
        t1 = self.spec["targets"][0]
        t2 = spec2["targets"][0]
        for key in COMPARE_KEYS:
            if key == "linker_scatter_file":
                # 生成器会把 scatter_file 同步写入 LDads/ScatterFile（uVision 生效位置），
                # 回读后 linker_scatter_file 即为该路径；比较"等效 scatter 路径"即可
                self.assertEqual(t1["linker_scatter_file"] or t1["scatter_file"],
                                 t2["linker_scatter_file"] or t2["scatter_file"],
                                 "roundtrip scatter 路径不一致")
                continue
            self.assertEqual(t1[key], t2[key], f"roundtrip 字段不一致: {key}")

    def test_update_preserves_unknown_fields(self):
        out = self.dir / "update.uvprojx"
        out.write_bytes(SAMPLE.encode("utf-8"))
        spec = {"targets": [{"name": "bootloader", "c_defines": ["NEW_DEF"]}]}
        uv_generator.update_project_file(out, spec, backup=False)
        text = out.read_text(encoding="utf-8")
        self.assertIn("<VendorSpecificKeepMe>42</VendorSpecificKeepMe>", text)
        self.assertIn("<PackID>Keil.STM32F1xx_DFP.2.2.0</PackID>", text)
        self.assertIn("<Define>NEW_DEF</Define>", text)
        self.assertIn('xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"', text)
        spec2 = uv_parser.parse_uvprojx(out)
        target = spec2["targets"][0]
        self.assertEqual(target["c_defines"], ["NEW_DEF"])
        self.assertEqual(target["c_include_paths"],
                         ["..\\core", "..\\port\\stm32f1\\f103c8t6"])
        self.assertEqual(target["groups"][0]["name"], "Core")
        self.assertFalse(target["toolset"]["uac6"])

    def test_backup_created_on_write(self):
        out = self.dir / "backup.uvprojx"
        out.write_bytes(SAMPLE.encode("utf-8"))
        spec = {"targets": [{"name": "bootloader", "c_defines": ["X"]}]}
        uv_generator.update_project_file(out, spec, backup=True)
        backups = list(self.dir.glob("backup.uvprojx.bak-*"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), SAMPLE.encode("utf-8"))

    def test_update_target_not_found(self):
        out = self.dir / "nomatch.uvprojx"
        out.write_bytes(SAMPLE.encode("utf-8"))
        spec = {"targets": [{"name": "no-such-target", "c_defines": ["X"]}]}
        with self.assertRaises(uv_generator.UvprojxGenerateError):
            uv_generator.update_project_file(out, spec, backup=False)

    def test_bad_root_rejected(self):
        bad = self.dir / "bad.xml"
        bad.write_text("<NotProject/>", encoding="utf-8")
        with self.assertRaises(uv_parser.UvprojxParseError):
            uv_parser.parse_uvprojx(bad)

    def test_cli_exit_codes(self):
        parser_py = str(HERE / "parser.py")
        missing = str(self.dir / "missing.uvprojx")
        r = subprocess.run([sys.executable, parser_py, missing],
                           capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        self.assertEqual(r.returncode, 1)

        generator_py = str(HERE / "generator.py")
        bad_spec = self.dir / "bad_spec.json"
        bad_spec.write_text(json.dumps({"targets": []}), encoding="utf-8")
        r = subprocess.run(
            [sys.executable, generator_py, str(bad_spec),
             "-o", str(self.dir / "x.uvprojx")],
            capture_output=True, text=True, encoding="utf-8", errors="replace")
        self.assertEqual(r.returncode, 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
