# Keil 命令行构建说明（build_keil）

## 一键构建（推荐，ADR-015 CSP）

```bash
CHIP=f103c8t6 bash scripts/build_keil.sh              # TARGETS 默认 "bootloader app"
CHIP=f103c8t6 TARGETS=bootloader bash scripts/build_keil.sh   # 只构建 BL
```

脚本动作（每个目标）：`chipfill.py` 由 `chips/<id>.json` + 模板生成 `<目标>.spec.json` 与
`linker/<目标>.sct` → `generator.py` 生成 `<目标>.uvprojx` → `UV4 -r` 全量重建 →
按退出码判定结果 → `fromelf` 生成 bin（BL 在仓库根，APP 在 `app/examples/<chip>_app/`）→
输出大小与 SHA-256。

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
python ../LiteTools/uvprojx/chipfill.py --chip chips/f103c8t6.json --target bootloader \
    --spec-out bootloader.spec.json --sct-out linker/bootloader.sct
python ../LiteTools/uvprojx/generator.py bootloader.spec.json -o bootloader.uvprojx
UV4 -r bootloader.uvprojx -j0 -o keil_build.log     # -r 全量重建，避免增量旧产物干扰
fromelf --bin --output=bootloader.bin Objects/bootloader.axf
```

## 注意事项

- 本工程用 **AC5**：不要在 Misc Controls 里加 AC6 专属选项（如 `-Oz`，AC5 报
  `C4056E: bad option`，19 个 C 文件全灭）；AC5 尺寸优化用 `-Ospace`。
- third_party/CMSIS 为 CMSIS V1.30 自包含内核头（CMSIS 6 已不支持 AC5），勿与
  CMSIS 6 头混用；细节见 `third_party/CMSIS/LICENSES.md` 与 `docs/design.md` ADR-013。
- **改配置改源头**：`<目标>.spec.json` 与 `linker/*.sct` 是 chipfill 的**生成产物**
  （已入库可复现），不要手改。芯片相关变更改 `chips/<id>.json`（或 `chips/templates/`
  内的 spec 模板），改完跑 `python chips/test_chip.py` 确认往返一致后重新构建。
  若在 Keil GUI 中调整过工程设置，用 `../LiteTools/uvprojx/parser.py` 解析 GUI 保存的
  工程，把 device 相关字段回填到 `chips/<id>.json`——下次构建会按清单重新生成全部产物。
