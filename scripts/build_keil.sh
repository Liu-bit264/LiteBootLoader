#!/usr/bin/env bash
# Keil 命令行构建（AC5）：chip.json+模板 -> spec/sct -> 生成工程 -> 全量重建 -> 退出码判定 -> 生成 bin
# 用法：CHIP=f103c8t6 [TARGETS="bootloader app"] bash scripts/build_keil.sh
#   CHIP    芯片清单 id（chips/<id>.json），默认 f103c8t6（ADR-015 CSP）
#   TARGETS 构建目标列表，默认 "bootloader app"
# 退出码含义（UV4）：0=无警告无错误，1=有警告，>=2=有错误
#
# 产物路径（chips/<id>.json build 段声明）：
#   artifact_dir  spec/uvprojx/bin 所在目录；"" = 仓库根（f103c8t6 legacy 槽位），
#                 新芯片为 chips/<id>（spec/sct/uvprojx 按芯片分槽位，互不覆盖）
#   sct_dir       散布加载文件目录（linker 或 linker/<id>）
set -u

CHIP="${CHIP:-f103c8t6}"
TARGETS="${TARGETS:-bootloader app}"
CHIPS="chips/${CHIP}.json"
UVTOOLS="${LITETOOLS_UVPROJX:-../LiteTools/uvprojx}"   # uvprojx 工具外置独立仓 LiteTools
UV4="${KEIL_UV4:-/e/Hardware/Keil/Keil_v5/UV4/UV4.exe}"
FROMELF="${KEIL_FROMELF:-/e/Hardware/Keil/Keil_v5/ARM/ARMCC/bin/fromelf.exe}"
LOG="${LOG:-keil_build.log}"

[ -f "$CHIPS" ] || { echo "[build] 芯片清单不存在: $CHIPS"; exit 2; }
[ -f "$UVTOOLS/chipfill.py" ] || { echo "[build] LiteTools 不存在: $UVTOOLS（克隆 https 位置后与主仓并列放置，或用 LITETOOLS_UVPROJX 指向）"; exit 2; }
# 产物槽位与 APP 目录均由芯片清单给出（反斜杠转正斜杠供 bash 使用）
IFS='|' read -r ARTDIR SCTDIR <<< "$(python - "$CHIPS" <<'EOF'
import json, sys
b = json.load(open(sys.argv[1], encoding='utf-8'))['build']
print(b.get('artifact_dir', '').replace('\\', '/') + '|' + b['sct_dir'].replace('\\', '/'))
EOF
)"
APP_DIR=$(python -c "import json;print(json.load(open('$CHIPS',encoding='utf-8'))['build']['app_example_dir'].replace(chr(92),'/'))")

out() {  # out <目录（可空=仓库根）> <文件名> -> 相对路径
  if [ -n "$1" ]; then printf '%s/%s' "$1" "$2"; else printf '%s' "$2"; fi
}
[ -n "$ARTDIR" ] && mkdir -p "$ARTDIR"
mkdir -p "$SCTDIR"

for TGT in $TARGETS; do
  echo "==== [${CHIP}] ${TGT} ===="
  SPEC_OUT=$(out "$ARTDIR" "${TGT}.spec.json")
  SCT_OUT=$(out "$SCTDIR" "${TGT}.sct")
  PRJ_OUT=$(out "$ARTDIR" "${TGT}.uvprojx")

  # 1) 芯片清单+模板 -> spec 与 scatter（构建侧唯一事实源，产物入库可复现）
  python "$UVTOOLS/chipfill.py" --chip "$CHIPS" --target "$TGT" \
      --spec-out "$SPEC_OUT" --sct-out "$SCT_OUT" || exit 1

  # 2) spec -> 工程文件（Objects 输出目录随 uvprojx 落位）
  python "$UVTOOLS/generator.py" "$SPEC_OUT" -o "$PRJ_OUT" || exit 1

  # 3) 全量重建（-r），避开增量构建的旧产物干扰
  #    UV4 -o 的日志路径相对工程文件目录解析——传绝对路径保证始终在仓库根可读
  rm -f "$LOG"
  "$UV4" -r "$PRJ_OUT" -j0 -o "$(pwd)/$LOG"
  rc=$?
  tail -n 4 "$LOG"
  case $rc in
    0) echo "[build] ${TGT} 成功：无警告无错误" ;;
    1) echo "[build] ${TGT} 成功但有警告："
       grep -iE "warning" "$LOG" | head -n 20 ;;
    *) echo "[build] ${TGT} 失败（退出码 $rc），错误摘要："
       grep -iE "error" "$LOG" | head -n 20
       rm -f "$LOG"
       exit "$rc" ;;
  esac

  # 4) 生成 bin 并核对
  case $TGT in
    bootloader) BIN=$(out "$ARTDIR" "bootloader.bin");;    # BL 产物随芯片槽位（f103 在仓库根）
    app)        BIN="${APP_DIR}/app.bin";;                 # APP 产物随芯片示例目录
    *)          echo "[build] 未知目标的产物路径: $TGT"; exit 2;;
  esac
  AXF=$(out "$ARTDIR" "Objects/${TGT}.axf")
  "$FROMELF" --bin --output="$BIN" "$AXF" || exit 1
  # 5) 分区限额校验（AGENTS §9.4 基线；审计 2026-09-29 P3-4）：超限判失败
  LIMIT=$(python -c "import json,sys; print(int(json.load(open(sys.argv[1], encoding='utf-8'))['partitions'][sys.argv[2]]['size'], 16))" "$CHIPS" "$TGT")
  SIZE=$(stat -c %s "$BIN")
  if [ "$SIZE" -gt "$LIMIT" ]; then
    echo "[build] $BIN: $SIZE 字节，超过 $TGT 分区限额 $LIMIT 字节——判失败"
    exit 1
  fi
  echo "[build] $BIN: $SIZE 字节（限额 $LIMIT）"
  sha256sum "$BIN"

  rm -f "$PRJ_OUT.bak-"*
done

# spec 为源，工程文件可复现，无需保留日志
rm -f "$LOG"
