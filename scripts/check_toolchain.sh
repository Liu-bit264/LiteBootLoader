#!/usr/bin/env bash
# LiteBootLoader 工具链检查脚本（Git Bash / POSIX shell；CMD 场景用 check_toolchain.bat）
# 逐项输出 可用（含版本）/ 缺失 / 未检测；可选工具缺失不判整体失败，恒以 0 退出。

set -u

OK=0; MISS=0; NA=0

have() { command -v "$1" >/dev/null 2>&1; }

report() { # report <名称> <可用|缺失|未检测> <说明>
  printf '%-18s [%s] %s\n' "$1" "$2" "$3"
  case "$2" in
    可用) OK=$((OK + 1)) ;;
    缺失) MISS=$((MISS + 1)) ;;
    *)    NA=$((NA + 1)) ;;
  esac
}

echo "=== LiteBootLoader 工具链检查 ==="

# CMake（可选：自动化验证）
if have cmake; then
  report "CMake" "可用" "$(cmake --version 2>/dev/null | head -n 1)"
else
  report "CMake" "缺失" "可选；GCC/CMake 自动化验证"
fi

# arm-none-eabi-gcc（可选：兼容编译验证）
if have arm-none-eabi-gcc; then
  report "arm-none-eabi-gcc" "可用" "$(arm-none-eabi-gcc --version 2>/dev/null | head -n 1)"
else
  report "arm-none-eabi-gcc" "缺失" "可选；源码级 GCC 兼容验证"
fi

# Make / Ninja（可选：GCC 构建后端）
MAKE_BIN=""
for m in make mingw32-make; do
  if have "$m"; then MAKE_BIN="$m"; break; fi
done
if [ -n "$MAKE_BIN" ]; then
  report "Make" "可用" "$MAKE_BIN $("$MAKE_BIN" --version 2>/dev/null | head -n 1)"
else
  report "Make" "缺失" "可选；GCC 构建后端"
fi

if have ninja; then
  report "Ninja" "可用" "$(ninja --version 2>/dev/null)"
else
  report "Ninja" "缺失" "可选；GCC 构建后端"
fi

# OpenOCD（可选：烧录调试）
if have openocd; then
  report "OpenOCD" "可用" "$(openocd --version 2>&1 | head -n 1)"
else
  report "OpenOCD" "缺失" "可选；烧录与调试"
fi

# Python + pyserial / Pillow
PYTHON_CMD=""
for cand in python python3 "py -3"; do
  if "$cand" --version >/dev/null 2>&1; then
    ver="$("$cand" --version 2>&1 | head -n 1)"
    case "$ver" in
      Python\ *) PYTHON_CMD="$cand"; PYTHON_VER="$ver"; break ;;
    esac
  fi
done

if [ -n "$PYTHON_CMD" ]; then
  report "Python" "可用" "$PYTHON_CMD：$PYTHON_VER"
  if "$PYTHON_CMD" -c "import serial" >/dev/null 2>&1; then
    report "pyserial" "可用" "上位机升级工具依赖"
  else
    report "pyserial" "缺失" "阶段 3 需要；用 uv 隔离运行：uv run --python 3.12 --with pyserial <脚本>（勿装 Miniforge base）"
  fi
  if "$PYTHON_CMD" -c "import PIL" >/dev/null 2>&1; then
    report "Pillow" "可用" "ICO 生成器图像处理依赖"
  else
    report "Pillow" "缺失" "可选；安装：pip install pillow"
  fi
else
  report "Python" "缺失" "上位机与脚本工具依赖"
  report "pyserial" "未检测" "依赖 Python"
  report "Pillow" "未检测" "依赖 Python"
fi

# uv（Astral；Python 依赖隔离运行器，本机安装于 E:\dev-tools\runtimes\uv）
# 本机约定：Miniforge base 不装包，依赖一律 uv run --with <pkg> 或 uv venv 隔离
if have uv; then
  report "uv" "可用" "$(uv --version 2>/dev/null)（$(command -v uv)）"
else
  report "uv" "缺失" "可选；本机常见位置 E:\\dev-tools\\runtimes\\uv"
fi

# Keil MDK UV4（主交付工具链；只探测文件存在与版本，不启动 UV4）
# 探测优先级：KEIL_UV4 环境变量 > 已知安装路径（含本机实际路径）> 根目录通配
UV4=""
if [ -n "${KEIL_UV4:-}" ] && [ -f "$KEIL_UV4" ]; then
  UV4="$KEIL_UV4"
fi
if [ -z "$UV4" ]; then
  for d in /c /d /e /f; do
    for p in "$d/Keil_v5/UV4/UV4.exe" "$d/Keil/UV4/UV4.exe" \
             "$d/Hardware/Keil/Keil_v5/UV4/UV4.exe" \
             "$d/Hardware/Keil_v5/UV4/UV4.exe"; do
      if [ -f "$p" ]; then UV4="$p"; break 2; fi
    done
  done
fi
if [ -z "$UV4" ]; then
  for p in /c/Keil*/UV4/UV4.exe /d/Keil*/UV4/UV4.exe \
           /e/Keil*/UV4/UV4.exe /f/Keil*/UV4/UV4.exe; do
    if [ -f "$p" ]; then UV4="$p"; break; fi
  done
fi

if [ -n "$UV4" ]; then
  UV4_WIN="$(cygpath -w "$UV4" 2>/dev/null || printf '%s' "$UV4")"
  UV4_VER="$(powershell.exe -NoProfile -Command \
    "(Get-Item -LiteralPath '$UV4_WIN').VersionInfo.FileVersion" 2>/dev/null \
    | tr -d '\r' | head -n 1)"
  if [ -n "$UV4_VER" ]; then
    report "Keil-UV4" "可用" "$UV4_WIN（版本 $UV4_VER）"
  else
    report "Keil-UV4" "可用" "$UV4_WIN（版本未检测）"
  fi
else
  report "Keil-UV4" "缺失" "主交付工具链；未找到 UV4.exe（非标准安装可设 KEIL_UV4 环境变量指向 UV4.exe）"
fi

echo
echo "--- 汇总：可用 $OK / 缺失 $MISS / 未检测 $NA ---"
echo "说明：Keil 为主交付路线，其余为可选验证与上位机工具；缺失不构成整体失败。"
exit 0
