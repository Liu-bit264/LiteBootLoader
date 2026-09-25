@echo off
rem LiteBootLoader 工具链检查（CMD 手工场景；Git Bash 请用 check_toolchain.sh）
rem 注意：本文件为 GBK 编码 + CRLF 换行，以匹配中文 Windows CMD 默认代码页；
rem       请勿转为 UTF-8（会导致中文乱码），也不要在脚本内 chcp 切换代码页
rem       （会导致 cmd 解析错位）。
rem Keil 探测优先级：KEIL_UV4 环境变量 > 已知安装路径（含本机实际路径）。
setlocal EnableDelayedExpansion

echo === LiteBootLoader 工具链检查 ===
echo.

for %%T in (cmake arm-none-eabi-gcc make mingw32-make ninja openocd) do (
    where %%T >nul 2>nul
    if errorlevel 1 (
        echo %%T                 [缺失] 可选
    ) else (
        echo %%T                 [可用]
    )
)

where python >nul 2>nul
if errorlevel 1 (
    echo Python               [缺失] 上位机与脚本工具依赖
    echo pyserial             [未检测] 依赖 Python
    echo Pillow               [未检测] 依赖 Python
) else (
    for /f "delims=" %%v in ('python --version 2^>^&1') do set "PYV=%%v"
    echo Python               [可用] !PYV!
    python -c "import serial" >nul 2>nul
    if errorlevel 1 (echo pyserial             [缺失] 阶段3用 uv run --python 3.12 --with pyserial 隔离运行) else (echo pyserial             [可用])
    python -c "import PIL" >nul 2>nul
    if errorlevel 1 (echo Pillow               [缺失] 可选: uv run --with pillow 隔离运行) else (echo Pillow               [可用])
)

where uv >nul 2>nul
if errorlevel 1 (
    echo uv                   [缺失] 可选: E:\dev-tools\runtimes\uv
) else (
    for /f "delims=" %%v in ('uv --version 2^>^&1') do set "UVV=%%v"
    echo uv                   [可用] !UVV!
)

set "UV4="
if defined KEIL_UV4 if exist "%KEIL_UV4%" set "UV4=%KEIL_UV4%"
if not defined UV4 (
    for %%D in (C D E F) do (
        if not defined UV4 if exist "%%D:\Keil_v5\UV4\UV4.exe" set "UV4=%%D:\Keil_v5\UV4\UV4.exe"
        if not defined UV4 if exist "%%D:\Keil\UV4\UV4.exe" set "UV4=%%D:\Keil\UV4\UV4.exe"
        if not defined UV4 if exist "%%D:\Hardware\Keil\Keil_v5\UV4\UV4.exe" set "UV4=%%D:\Hardware\Keil\Keil_v5\UV4\UV4.exe"
        if not defined UV4 if exist "%%D:\Hardware\Keil_v5\UV4\UV4.exe" set "UV4=%%D:\Hardware\Keil_v5\UV4\UV4.exe"
    )
)
if defined UV4 (
    set "UV4V="
    for /f "delims=" %%v in ('powershell -NoProfile -Command "(Get-Item -LiteralPath '!UV4!').VersionInfo.FileVersion" 2^>nul') do set "UV4V=%%v"
    if defined UV4V (
        echo Keil-UV4             [可用] !UV4! 版本 !UV4V!
    ) else (
        echo Keil-UV4             [可用] !UV4! 版本未检测
    )
) else (
    echo Keil-UV4             [缺失] 主交付工具链: 未找到 UV4.exe（非标准安装可设 KEIL_UV4 环境变量）
)

echo.
echo 检查完成（缺失可选工具不构成失败；Python 依赖一律 uv 隔离运行，勿装 Miniforge base）。
endlocal
exit /b 0
