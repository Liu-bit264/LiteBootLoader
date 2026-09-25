# Keil 命令行构建说明（build_keil）

## 一键构建（推荐）

```bash
bash scripts/build_keil.sh
```

脚本动作：按 `bootloader.spec.json` 重新生成 `bootloader.uvprojx` → `UV4 -r` 全量重建 →
按退出码判定结果 → `fromelf` 生成 `bootloader.bin` → 输出大小与 SHA-256。

## 工具链（AC5，本项目固定）

| 工具 | 本机路径 | 可用环境变量覆盖 |
|---|---|---|
| UV4 | `E:\Hardware\Keil\Keil_v5\UV4\UV4.exe` | `KEIL_UV4` |
| fromelf（AC5 版） | `E:\Hardware\Keil\Keil_v5\ARM\ARMCC\bin\fromelf.exe` | `KEIL_FROMELF` |
| 编译器 | ARMCC V5.06 update 7 (build 960) | —（工程内 `uAC6=0` + `pCCUsed` 固定） |

## UV4 退出码判定（必须按退出码，不能只看是否生成文件）

| 退出码 | 含义 |
|---|---|
| 0 | 无错误无警告 |
| 1 | 有警告 |
| ≥2 | 有错误 |

## 手工等价命令

```bash
python tools/uvprojx/generator.py bootloader.spec.json -o bootloader.uvprojx
UV4 -r bootloader.uvprojx -j0 -o keil_build.log     # -r 全量重建，避免增量旧产物干扰
fromelf --bin --output=bootloader.bin Objects/bootloader.axf
```

## 注意事项

- 本工程用 **AC5**：不要在 Misc Controls 里加 AC6 专属选项（如 `-Oz`，AC5 报
  `C4056E: bad option`，19 个 C 文件全灭）；AC5 尺寸优化用 `-Ospace`。
- third_party/CMSIS 为 CMSIS V1.30 自包含内核头（CMSIS 6 已不支持 AC5），勿与
  CMSIS 6 头混用；细节见 `third_party/CMSIS/LICENSES.md` 与 `docs/design.md` ADR-013。
- 若在 Keil GUI 中调整过工程设置，请把变更同步回 `bootloader.spec.json`
  （GUI 保存会覆盖生成文件；下次运行脚本会按 spec 重新生成）。
