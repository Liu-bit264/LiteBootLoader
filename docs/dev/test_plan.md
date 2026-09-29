# 测试计划（Test Plan）

> 对应 ../AGENTS.md §12 阶段 4 与 §13 验收标准。测试分四级：L1 主机侧工具自测、
> L2 硬件在环自动化（selftest）、L3 场景 E2E、L4 验收对照。
> 日期基准 2026-09-26；环境：Blue Pill F103C8T6 + DAPLink（COM4）+ UartAssist/pyserial。

## 1. 测试环境与前置

| 项 | 值 |
|---|---|
| 硬件 | STM32F103C8T6 最小系统板（8 MHz 晶振）、DAPLink（CMSIS-DAP + CDC）、OLED SSD1306（PB8/PB9） |
| 串口 | COM4，115200 8N1（DAPLink CDC）；测试期间必须关闭 UartAssist 等占用者 |
| 构建 | `bash scripts/build_keil.sh`（UV4 退出码判定：0=无警告无错误） |
| 烧录 BL | `pyocd flash --target stm32f103c8 --pack <DFP> --base-address 0x08000000 bootloader.bin`，**会话结束必须 `-c "reset"` 恢复运行**（否则内核停在调试停机态，串口全静默） |
| 上位机 | `uv run --python 3.12 --with pyserial ../LiteBootUpgrader/bl_upgrade.py <子命令> --port COM4`（独立上位机仓，依赖隔离，勿 pip 直装） |

## 2. L1 主机侧工具自测（无需硬件）

| 测试 | 命令 | 通过标准 | 状态 |
|---|---|---|---|
| uvprojx 解析/生成往返 | LiteTools 仓 `uvprojx/test_uvprojx.py`（`../LiteTools`） | 全部断言通过 | ✅ 通过（阶段 0 交付起持续可复跑） |
| ICO 解析/生成 | LiteTools 仓 `ico/test_ico.py` | 全部断言通过 | ✅ 通过 |

## 3. L2 硬件在环自动化（selftest，15 步）

命令：`uv run --python 3.12 --with pyserial ../LiteBootUpgrader/bl_upgrade.py selftest --port COM4`

| # | 步骤 | 覆盖点 |
|---|---|---|
| 1 | PING | 协议帧往返、VER 回显 |
| 2 | ERASE_APP | 46 页整片擦除、页间喂狗（实测 ~1.1 s） |
| 3-4 | 读路径探针 verify(1024/512, 全FF) | **CRC32 实现与 zlib 逐位一致**、擦除有效性、Flash 读路径 |
| 5 | WRITE 4B @0 | 最小写入 |
| 6 | VERIFY 4B 回读 | 写入内容与图案一致 |
| 7-9 | WRITE 248B/252B/8B | 分块写入、已擦页位图（同页不互抹） |
| 10 | VERIFY 512B 全量 | 内容 + 持久化前校验 |
| 11 | GET_META | app_size/app_crc/seq/active_copy 持久化正确 |
| 12 | WRITE 越界 | offset=APP_SIZE → RANGE_ERROR |
| 13 | 坏 CRC 帧 | 静默丢弃（无响应），计数器 crcfail 不动 |
| 14 | SET_META bl_request 1→0 | 参数区掉电安全写往返、flags bit0 生命周期 |
| 15 | JUMP_APP（无效 APP） | STATE_ERROR 拒绝且不跳转 |

**验收门槛：连续 ≥7 轮 15/15 全绿**（历史：时间戳回绕 bug 修复前大帧失败率 ~50-100%/轮，修复后 7 连绿）。
遥测辅助：GET_INFO 扩展字段 `rx/vf/crcfail/bytetimeout/超时现场四元组` 用于失败归因（../protocol.md §4.2）。

## 4. L3 场景 E2E（真 APP 链路）

| 场景 | 步骤 | 通过标准 | 状态 |
|---|---|---|---|
| 协议升级 | `upgrade app/examples/f103c8t6_app/app.bin` | erase OK → 逐块写入零丢失 → verify CRC 匹配 → 元数据持久化 | ✅ 通过（22 块零丢失） |
| 命令跳转 | `jump` | OK → 九步跳转 → APP banner `A:APP v0.1.0 running, breathing` | ✅ 通过 |
| APP 存活 | `ping`（对 APP 响应器） | status=OK proto_ver=0x01 | ✅ 通过 |
| APP 请求回 BL | `setmeta 01 01` | APP 回 OK → 复位 → `I:BL request -> upgrade mode` → GET_META flags bit0=0（已消费） | ✅ 通过 |
| 复位自动跳转 | `reset` | `I:APP valid, wait 3000ms` → 3 s 窗 → 自动九步跳转 → APP banner；期间心跳正常 | ✅ 通过 |
| 无效 APP 拒跳 | APP 区写入非程序数据后上电 | `app_valid=0`，停留升级模式（LED 双闪） | ✅ 通过（selftest 图案数据期实测） |
| 升级中断恢复 | 升级中途拔电 → 重上电 → 重新 `upgrade` | BL/参数区完好，可完整重升 | ✅ 通过（2026-09-26 写入中途 pyocd 复位注入：VERIFY 正确检出不完整内容并拒绝，重新 upgrade 恢复 + JUMP 成功；真实拔电为等效补充） |
| 断电参数区恢复 | 写参数区瞬间拔电 → 重上电 | 双副本取最新有效副本，seq 连续 | ✅ 通过（2026-09-26 复位注入钻具 10/10 轮，全部命中在途写入，见 §5 #9） |
| IWDG 长跑 | APP 连续运行 ≥10 min | 无误复位（呼吸灯连续、串口无重启横幅） | ✅ 通过（2026-09-26 实测 600 s：APP 重启横幅 0 次、BL 复位日志 0 次） |
| VOFA+ 观察 | RawData 引擎手动发帧 | 日志可读、PING 帧往返可通 | ✅ 通过（2026-09-26 实机首跑，见 §5 #11） |

## 5. L4 验收对照表（../AGENTS.md §13）

| # | 验收项 | 结论 | 证据 |
|---|---|---|---|
| 1 | BL 编译通过，bin ≤16 KiB | ✅ 通过 | 13 728 B（SHA-256 ce41b1b6…181bb1，review 2026-09-27 修复后），AC5 0 错 0 警 |
| 2 | APP 编译通过，bin ≤46 KiB | ✅ 通过 | 8 072 B（SHA-256 1ab705d6…fc4d574，review 加固后重建），0 错 0 警 |
| 3 | 正常升级后跳转 APP，中断正常 | ✅ 通过 | L3 场景 1/2/5；APP SysTick+USART1 中断随呼吸灯/响应器运行 |
| 4 | PC13 LED 与串口日志符合状态定义 | ✅ 通过 | BL 五种 LED 模式 + `I:` 日志；APP 呼吸灯 + `A:` 提示 |
| 5 | OLED 显示版本/芯片/APP 状态/进度/CRC/IWDG | ✅ 通过（阶段 3 联测复核） | 版本/芯片/模式/RX/VF 实测；进度/CRC 行升级期显示 |
| 6 | APP CRC 错误拒绝跳转进升级模式 | ✅ 通过 | 图案数据期 app_valid=0 + 校验关卡逐级拒绝 |
| 7 | 升级中断/复位后可重升，不误写 BL 与参数区 | ✅ 通过 | 已擦页位图 + is_range_valid 防御 + 多次重升实测；写入中途复位注入实测（2026-09-26，VERIFY 正确检出损坏后重升恢复）；真实拔电步骤见 §5 #9 |
| 8 | APP 主动请求进 BL，生命周期有文档与测试 | ✅ 通过 | ../external_interface.md §5 + L3 场景 4 |
| 9 | 参数区双副本断电恢复 | ✅ 通过 | 复位注入钻具 10/10 轮（2026-09-26）：SET_META 写入风暴中每轮在在途写入中间注入复位，R1~R4 恢复不变量全过、seq 单调、副本交替正常、修复写入成功、APP 区 VERIFY 未受牵连；工具 `../LiteBootUpgrader/bl_powerloss_drill.py`（独立上位机仓）。真实拔电（人工）补充步骤：升级期或 `bl_powerloss_drill.py` 运行中拔掉 USB → 重新上电 → `info` 应报出有效元数据 → `upgrade` 重升应成功 |
| 10 | BL→APP IWDG 接管无误复位 | ✅ 通过 | 跳转后 APP 持续运行（呼吸灯），跳转前喂狗 |
| 11 | VOFA+ 观察 + Python 工具完成升级 | ✅ 通过 | Python 工具 ✅（v1.1.1，独立仓 LiteBootUpgrader，CLI+GUI 全链路）；VOFA+ 实机首跑 PING 往返逐字节正确（2026-09-26，响应 CRC E8 69 与 CRC16/MODBUS 计算一致） |
| 12 | ../external_interface.md 与 ../porting_guide.md 完整 | ✅ 通过 | 两文档已交付（2026-09-26） |
| 13 | uvprojx 与 ICO 工具可运行测试 | ✅ 通过 | L1 自测 + uvprojx 生成器全程实战（BL/APP 两工程） |
| 14 | 版本与提交符合 SemVer/Conventional Commits | ✅ 通过 | 提交历史 feat/fix/chore + scope 规范 |

**汇总**：通过 14 项、部分 0 项、未通过 0 项（2026-09-26 全部关闭；#9/#7 的"真实拔电"为复位注入等效验证 + 人工拔电补充步骤已文档化）。

### 5.1 F411CEU6 最小包验收对照（ADR-017）

**2026-09-29 上板 HIL 实测完成**（板：F411CEU6 核心板 + DAPLink COM4；烧录走
`scripts/pyocd_manual_flash.py`——该板 pyocd flash 算法路径一启动即 FAULT，寄存器级
驱动全程稳定，CPUID=0x410FC241）：

| 项 | 结论 | 证据 |
|---|---|---|
| `chips/test_chip.py` 全芯片一致性（含 F4 非均匀扇区表两侧比对、双副本独立单元、引脚声明段） | ✅ 通过 | 12/12（f103c8t6 + f411ceu6） |
| BL 编译 0 错 0 警，bin ≤ 32K（本芯片分区） | ✅ 通过 | 12 536 B（SHA-256 `ebde0e74…`，引脚抽象 ADR-018 后），AC5 全量重建；烧录回读逐字节一致 |
| APP 编译 0 错 0 警，bin ≤ 448K | ✅ 通过 | 6 356 B（SHA-256 `46c711e4…`） |
| F103 回归不回退 | ✅ 通过 | 引脚抽象前与 0.2.0 基线逐字节一致（`499de4bc…`/`534eb656…`）；抽象后行为等价微增 BL 15 400 B（`31dc570f…`）/ APP 8 228 B（`3d5301c4…`），0 错 0 警限额内 |
| GET_INFO（sysmem 地址实测） | ✅ 通过 | `BL v0.2.0 flash=512KB`（FLSIZE@0x1FFF7A22 正确）、UID 96bit 读出（0x1FFF7A10） |
| selftest 15 步 | ✅ 14 PASS + 1 假阳性 | 唯一 FAIL「WRITE 越界防护」为 LBU 夹具硬编码 F103 `APP_SIZE=0xB800`（bl_upgrade.py:38）——该偏移在 F411 448K APP 区内，固件接受写入是正确行为；同源欠账见下 |
| ERASE/WRITE/VERIFY 全链路（IWDG 8s 放宽实测） | ✅ 通过 | 448K APP 区擦除 4.21s（含 128K 扇区 CPU 停顿期）全程无复位；VERIFY CRC32 `0x61a5df89` 与镜像一致 |
| JUMP + APP 运行 | ✅ 通过 | 呼吸灯目视 + 串口横幅 `A:F411 APP v0.1.0 running, breathing`（SysTick+USART1 中断运行） |
| APP 请求回 BL 闭环 | ✅ 通过 | `setmeta 01 01` → 落盘复位 → BL 重启 `app_valid=1 seq=3`；GET_META seq 单调（3→7） |
| 升级中断恢复（复位注入） | ✅ 通过 | 擦除进行中经 SWD 注入复位 → LBU 幂等重发（`erase 超时重发 2/3`）→ 同轮完成；随后完整重升成功；BL/参数区无恙 |
| 上电自跳转（有效 APP） | ✅ 通过 | 复位 → `wait 3000ms` 心跳 5 拍 → 自动跳转 → APP 横幅（ADR-004 路径实测） |
| 100MHz/3WS 时钟 | ✅ 通过（间接） | 心跳 500ms 节拍精准（SysTick 按 SystemCoreClock=100M 分频）、115200 帧全程零 CRC 失败 |
| 蓝牙 / OLED / OTA_QUERY 蓝牙字段 | ➖ 不适用 | 最小包无蓝牙/OLED；`BL_TRANSPORT_BT_EN=0`，PC14 为 NC 保留位 |
| 真实拔电演练（人工） | ⬜ 待办 | LBU 断电钻具 `--chip` 参数化（PYOCD_CMD 写死 stm32f103c8）属跨仓欠账；本表复位注入为其等效验证 |

**跨仓欠账（LBU）**：selftest/断电钻具的芯片参数化——`APP_SIZE=0xB800`、
`PYOCD_CMD` 目标名均硬编码 F103，需 LBU 侧 PR 支持 `--chip`/GET_INFO 派生。

**板卡实测注意（本板）**：正反面丝印不一致（SWD 引脚以**背面**为准）；供电不足会
导致 SWD 无应答（插 Type-C 供满后恢复）；pyocd flash 算法路径不可用，烧录用
`scripts/pyocd_manual_flash.py`。

## 6. 常见问题与风险（实测教训索引）

| 现象 | 根因 | 对策 |
|---|---|---|
| 大帧写入随机静默失败 | 协议 poll 用循环顶旧时间戳，毫秒边界跨越时无符号差值回绕假超时 | 已修（现场重读时刻）；阈值 2000 ms；上位机重试间隔 ≥2 s |
| 同一页后面的分块消失 | 旧版写前无条件扫描擦页，同页后写抹掉先写 | 已修（已擦页位图）；verify 回读 CRC 可复证 |
| 跳转后 APP 静默硬fault | APP SystemInit 在 72 MHz 下把 Flash 降 0WS | 已修（SWS=PLL 早退路径） |
| 跳转后随机跳址 HardFault | C 函数 `__set_MSP` 后自身尾声从新栈弹 PC | 已修（bl_jump.s 原子序列）；移植时严禁在 C 里切 MSP 后继续执行 |
| 串口全静默（疑似死机） | 调试器会话把内核留在停机态 | pyocd 会话结束必须显式复位；先读 DHCSR bit17 |
| 串口乱码但 LED/OLED 正常 | 波特率与时钟不匹配 | 读 OLED CLK 行/GET_INFO；确认 `bl_clock_get_hz()` 与实际一致 |
| 串口数据被污染 | 劣质 USB 链路（拓展坞等） | 直连 DAPLink；时序判据先确认链路可信 |
| 升级无认证（安全边界） | 协议仅 CRC16/CRC32 完整性校验，无签名 | review P3 确认：显式非目标（design.md §1）；认证接入点预留于 storage 校验链 + 元数据，生产部署前必须评估 |
| 呼吸灯变"常亮+频闪"（接入 OLED 后） | 亮度条每 100ms 弄脏一页，软 I2C 发一页阻塞主循环 ~13ms（100kHz），PWM 被搅出 10Hz 频闪；老曲线 bright≥10 段本为 100% 占空比即"常亮" | 已修（2026-09-26）：LED PWM 移入 `bl_systick_user_hook`（SysTick 1ms 中断，弱符号覆盖，主循环阻塞免疫）；呼吸曲线改 0..9 级（占空比 ≤90%，无 100% 平台）。教训：凡主循环可能被阻塞的固件，严格节拍类输出必须进中断 |
