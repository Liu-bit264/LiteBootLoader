#!/usr/bin/env bash
# Keil 命令行构建（AC5）：chip.json+模板 -> spec/sct -> 生成工程 -> 全量重建 -> 退出码判定 -> 生成 bin
# 用法：CHIP=f103c8t6 [TARGETS="bootloader app"] bash scripts/build_keil.sh
#   CHIP    芯片清单 id（chips/<id>.json），默认 f103c8t6（ADR-015 CSP）
#   TARGETS 构建目标列表，默认 "bootloader app"
# 退出码含义（UV4）：0=无警告无错误，1=有警告，>=2=有错误
set -u

CHIP="${CHIP:-f103c8t6}"
TARGETS="${TARGETS:-bootloader app}"
CHIPS="chips/${CHIP}.json"
UV4="${KEIL_UV4:-/e/Hardware/Keil/Keil_v5/UV4/UV4.exe}"
FROMELF="${KEIL_FROMELF:-/e/Hardware/Keil/Keil_v5/ARM/ARMCC/bin/fromelf.exe}"
LOG="${LOG:-keil_build.log}"

[ -f "$CHIPS" ] || { echo "[build] 芯片清单不存在: $CHIPS"; exit 2; }
# APP 产物目录由芯片清单给出（app\examples\<chip>_app，转正斜杠供 bash 使用）
APP_DIR=$(python -c "import json;print(json.load(open('$CHIPS',encoding='utf-8'))['build']['app_example_dir'].replace(chr(92),'/'))")

for TGT in $TARGETS; do
  echo "==== [${CHIP}] ${TGT} ===="

  # 1) 芯片清单+模板 -> spec 与 scatter（构建侧唯一事实源，产物入库可复现）
  python tools/uvprojx/chipfill.py --chip "$CHIPS" --target "$TGT" \
      --spec-out "${TGT}.spec.json" --sct-out "linker/${TGT}.sct" || exit 1

  # 2) spec -> 工程文件
  python tools/uvprojx/generator.py "${TGT}.spec.json" -o "${TGT}.uvprojx" || exit 1

  # 3) 全量重建（-r），避开增量构建的旧产物干扰
  rm -f "$LOG"
  "$UV4" -r "${TGT}.uvprojx" -j0 -o "$LOG"
  rc=$?
  tail -n 4 "$LOG"
  case $rc in
    0) echo "[build] ${TGT} 成功：无警告无错误" ;;
    1) echo "[build] ${TGT} 成功但有警告，请检查日志" ;;
    *) echo "[build] ${TGT} 失败（退出码 $rc），错误摘要："
       grep -iE "error" "$LOG" | head -n 20
       rm -f "$LOG"
       exit "$rc" ;;
  esac

  # 4) 生成 bin 并核对
  case $TGT in
    bootloader) BIN="bootloader.bin";;                 # BL 产物在仓库根（烧录入口）
    app)        BIN="${APP_DIR}/app.bin";;             # APP 产物随芯片示例目录
    *)          echo "[build] 未知目标的产物路径: $TGT"; exit 2;;
  esac
  "$FROMELF" --bin --output="$BIN" "Objects/${TGT}.axf" || exit 1
  echo "[build] $BIN: $(stat -c %s "$BIN") 字节"
  sha256sum "$BIN"

  rm -f "${TGT}.uvprojx.bak-"*
done

# spec 为源，工程文件可复现，无需保留日志
rm -f "$LOG"
