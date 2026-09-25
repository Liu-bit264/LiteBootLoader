#!/usr/bin/env bash
# Keil 命令行构建（AC5）：重新生成工程 -> 全量重建 -> 退出码判定 -> 生成 bin
# 退出码含义（UV4）：0=无警告无错误，1=有警告，>=2=有错误
set -u

UV4="${KEIL_UV4:-/e/Hardware/Keil/Keil_v5/UV4/UV4.exe}"
FROMELF="${KEIL_FROMELF:-/e/Hardware/Keil/Keil_v5/ARM/ARMCC/bin/fromelf.exe}"
LOG="${LOG:-keil_build.log}"

# 1) 按 spec 重新生成工程（spec 是唯一事实源）
python tools/uvprojx/generator.py bootloader.spec.json -o bootloader.uvprojx || exit 1

# 2) 全量重建（-r），避开增量构建的旧产物干扰
rm -f "$LOG"
"$UV4" -r bootloader.uvprojx -j0 -o "$LOG"
rc=$?
tail -n 4 "$LOG"
case $rc in
  0) echo "[build] 成功：无警告无错误" ;;
  1) echo "[build] 成功但有警告，请检查日志" ;;
  *) echo "[build] 失败（退出码 $rc），错误摘要："
     grep -iE "error" "$LOG" | head -n 20
     rm -f "$LOG"
     exit "$rc" ;;
esac

# 3) 生成 bin 并核对
"$FROMELF" --bin --output=bootloader.bin Objects/bootloader.axf || exit 1
echo "[build] bootloader.bin: $(stat -c %s bootloader.bin) 字节"
sha256sum bootloader.bin

# spec 为源，工程文件可复现，无需保留备份
rm -f bootloader.uvprojx.bak-* "$LOG"
