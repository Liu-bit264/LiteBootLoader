# LiteBootLoader 改进建议

> 生成日期：2026-09-26
> 依据：对 core / port / services / bsp / app / linker / tools / docs 的只读审查，
> 以及本机实际执行的 Python 工具测试（uvprojx 7/7、ico 7/7 通过）与已提交构建产物尺寸核验。
> 说明：本文只列改进项，不含已实现代码的重写。优先级 P1（重要）> P2 > P3（次要）。

## 0. 总体结论

代码结构成熟、分层清晰（core/port/services/bsp 解耦，core 不依赖 HAL、无动态内存），
九步跳转以真汇编原子完成，CRC 常量经自检，参数区双副本掉电安全写状态机与文档一致。
已核验构建产物在预算内：

| 产物 | 实测大小 | 限额 | 结论 |
|---|---:|---:|---|
| `bootloader.bin` | 12,840 B | 16 KiB (16,384 B) | 通过 |
| `app/examples/f103c8t6_app/app.bin` | 7,856 B | 46 KiB (47,104 B) | 通过 |

以下建议以健壮性与效率为主，非正确性缺陷。

---

## 1. 改进项清单

### [P2] 空闲等待循环每轮重算整片 APP CRC32

- **位置**：`core/bl_core.c` `bl_core_run()` 的 `BL_STATE_UPGRADE_WAIT` 分支；
  `core/bl_boot.c` `bl_boot_app_valid()`。
- **现象**：升级等待态下，只要协议不活跃，每轮循环都会调用 `bl_boot_app_valid()`，
  而该函数会读取并 CRC32 整个 APP 镜像（最多 46 KiB），并重新 `bl_meta_load()`。
- **影响**：功能安全（CRC 循环内有喂狗点，不会误复位），但持续空转烧 CPU 周期，
  且反复读 flash 计算一个空闲期不可能变化的值。
- **建议**：进入升级等待态时算一次并缓存有效性结果，仅在 ERASE / WRITE / VERIFY
  之后失效重算。可显著降低空闲功耗与 flash 读负载，且不改变任何状态语义。
- **优先级理由**：本文列出的最高价值改动，改动局部、风险低。

### [P3] 写进度使用分块起始偏移而非结束偏移

- **位置**：`core/bl_core.c` `handle_write()`。
- **现象**：进度计算为 `(offset * 100) / BL_APP_SIZE`，用的是分块起始 `offset`，
  导致进度条滞后一个分块；落在末尾的最后一块也会显示偏低。
- **影响**：纯显示，不影响升级逻辑。
- **建议**：改用 `offset + len` 反映已送达量，进度更贴合真实。

### [P3] APP 侧帧重组器可被 DATA/CRC 内的 `0x55 0xAA` 字节对误触发

- **位置**：`app/examples/f103c8t6_app/app_request.c` `try_parse()`。
- **现象**：DATA 或 CRC 字段内部若出现 `0x55 0xAA`，会提前命中 EOF 判定；
  长度不符时仅 `return` 继续累积，恢复依赖 267 字节超长复位与主机重试。
- **影响**：作为示例响应器可接受，但边界不如 BL 的流式解析器严谨。
- **建议**：在代码注释中明确标注此解析器为“最小示例、非 BL 流式解析器”，
  避免被复用到生产路径；如需增强可改为与 BL 一致的字节流状态机。

### [P3] 升级路径仅做完整性校验，无认证

- **位置**：升级协议整体（`core/bl_protocol.c` + `core/bl_storage.c` 校验链）。
- **现象**：仅有 CRC16（帧）与 CRC32（镜像）完整性校验，无签名/认证；
  USART1 上任意主机均可烧写任意代码。
- **影响**：符合当前阶段范围（本期不含加密技术栈），但属安全边界事实。
- **建议**：在验收/接口文档的风险章节将“无固件认证”显式列为非目标，
  而非留白；后续如引入签名，接入点应落在 storage 校验链与元数据结构。

---

## 2. 已核验为正确的关键设计（无需改动）

- **参数区双副本掉电安全**：擦目标页 → 写 → 回读 + seq 校验，交替写页，
  seq 回绕从 1 重启，出厂态首写页 A。与 `partition.md` 一致。
- **九步跳转顺序**：关中断 → 停 SysTick → 反初外设 → 清 pending → 设 VTOR →
  切 MSP 并跳转；MSP 切换与 BX 隔离在 `bl_jump.s`，规避了“从新栈 POP”导致的 HardFault。
- **协议解析器**：支持 SOF 重同步、非法长度拒绝、`bl_protocol_poll` 现场重读时钟做帧内超时、
  CRC 失败静默丢弃。
- **`handle_get_info`**：逐字段复核共写入 67 字节到 `d[67]`，无越界。
- **Keil 工程源文件清单**：BL 与 APP 工程均与磁盘树一致，含 ADR-014 的 `services/` 拆分。

---

## 3. 本环境未执行/未验证项（复现命令）

以下项因环境限制未在本机执行，结论标注为“未验证”，不得推断为通过：

- **Keil / GCC 构建**：本环境无 ARM 工具链，未重新构建；已提交的 `.bin` 尺寸是唯一构建证据。
  - 复现（Keil）：`UV4 -b bootloader.uvprojx -j0 -o build_bl.log` 后按退出码判定
    （0=无错无警告，1=有警告，≥2=有错误）。
- **`scripts/check_toolchain.sh`**：在本 PowerShell 沙箱内 Git Bash 调用失败，未取到输出。
  - 复现：Git Bash 下 `bash scripts/check_toolchain.sh`。
- **ICO 工具 Pillow 缩放路径**：包索引网络受限，跳过；本次通过的是无 Pillow 回退路径。
  - 复现：`uv run --python 3.12 --with pillow python tools/ico/test_ico.py`（需可达 PyPI 镜像）。
- **已实际执行并通过**：
  - `uv run --python 3.12 python tools/uvprojx/test_uvprojx.py` → 7/7 OK
  - `uv run --python 3.12 --offline python tools/ico/test_ico.py` → 7/7 OK

---

## 4. 建议的下一步

优先处理 **第 1 项**（空闲等待态缓存 APP 有效性）：它移除了空闲期对整片 flash 的持续 CRC 重算，
改动局部、不触动任何状态语义，是投入产出比最高的一处。

---

## 5. 处理记录（2026-09-26，ZCode 复核并实施）

| 项 | 处置 | 证据 |
|---|---|---|
| [P2] 空闲期重算 CRC32 | **已修**：`bl_core.c` 增加 `s_app_valid` 缓存——init 时取值、ERASE/WRITE 置脏、VERIFY 通过即置有效、UPGRADE_WAIT 空闲期读缓存不再重算 | 构建 0 错 0 警，bin 12,972 B（SHA `f08f6d46…`）；selftest 15/15（覆盖 缓存→擦/写置脏→VERIFY 置有效 全生命周期）；真实 APP 升级+跳转+PING 回归通过 |
| [P3] 写进度起始偏移 | **已修**：进度改按 `offset + len − 4`（已送达量）计算，并对非法 offset 先夹取——顺带消除原 `offset * 100` 在越界请求下的乘法回绕隐患 | selftest 越界防护步 PASS |
| [P3] APP 重组器边界 | **已加注**：`app_request.c` `try_parse()` 注明"最小示例重组器、DATA/CRC 内 0x55 0xAA 会提前截断、勿复用到生产路径" | 代码注释 |
| [P3] 无认证非目标 | **已文档化**：design.md §1 非目标、user_manual §9 注意事项、test_plan §6 风险表三处显式声明，并注明认证接入点（storage 校验链 + 元数据结构） | 文档 |
| §3 未验证项 | **已补验**：`bash scripts/check_toolchain.sh` Git Bash 下全绿（仅 base 无 pyserial，系 uv 隔离约定使然）；`uv run --python 3.12 --with pillow python tools/ico/test_ico.py` → 7/7 OK | 本次运行 |

补充观察：烧录后立即首轮 selftest 曾出现一次 verify 探针收到 1 字节响应的抖动
（紧跟 pyocd flash+reset 的会话；DAPLink CDC 当日多次不稳），强制回 BL 后复跑
15/15 通过且后续升级链路全绿，判定为工具链路瞬态而非固件缺陷；如复现再立卡排查。
