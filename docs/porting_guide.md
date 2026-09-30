# 移植指南（Porting Guide）

> 目标读者：要把本 BootLoader 框架迁移到其它芯片（F4 / G0 / H7 等）的工程师。
> 原则：**core 层不含任何芯片差异**——ADR-015 后 core 只经擦除单元接口访问 Flash，
> 芯片差异收敛到 `port/<家族>/<型号>/`、`chips/<id>.json` 与 `board_config.h`。
> 本指南来自 F103C8T6 首发移植的实测经验，§6/§10 的陷阱均为硬件实锤，移植时必须遵守。

## 1. 分层与依赖边界

```text
core/   状态机、协议、存储、元数据、启动决策 —— 只依赖 bl_port.h 的 ops 抽象 + board_config.h 常量
port/   芯片相关：时钟、Flash、UART、GPIO、IWDG、SysTick、跳转原子序列
bsp/    板级器件（如 OLED）
app/    示例应用（同样通过 ops 使用 port，不直接摸寄存器之外的芯片细节）
```

- core 禁止 `#include` 任何芯片/CMSIS 头（architecture.md §2）。
- 所有引脚、分区、超时、波特率常量唯一出处为 `board_config.h`，禁止散落硬编码。
- `third_party/CMSIS` 按目标芯片更换并续记 LICENSES.md（原样拷贝，可用 `scripts/vendor_copy.py`
  拷入并出 SHA-256 核验）；注意编译器支持——本项目固定 AC5：CMSIS 6 已弃 AC5，F1（M3）用
  V1.30 自包含内核头，F4（M4）用 Core(M) V5.6.0（保留 `cmsis_armcc.h`）+ 对应器件头，
  详见 third_party/CMSIS/LICENSES.md 选型说明。

## 2. 加一颗芯片的标准流程（CSP，ADR-015）

目标状态：**加芯片 = 建目录 + 填 chip.json + 实现 ops，构建零手工**。

1. 建 `port/<家族>/<型号>/`，复制 F103 实现作为骨架；家族公共件（如 F1 的 `f1_bits.h`、
   未来的 F4 电源序列）放 `port/<家族>/common/`。
2. 换启动文件（向量表、堆栈）与 CMSIS 器件头（平铺放 `third_party/CMSIS/`，续
   LICENSES.md 原样拷贝记录）；确认 `RESET` 段仍被 scatter 以 `*.o (RESET, +First)` 置于镜像首。
3. 写 `chips/<id>.json`：device（含 DFP flash_driver/register_file/sfd_file——可先在
   Keil GUI 配好设备再用 LiteTools 仓的 `uvprojx/parser.py` 解析现成工程提取）、memory、partitions
   （含参数双副本单元）、erase_units（**F1 均匀页**：`uniform:true, base, unit_size, count`；
   **F4 非均匀扇区**：`uniform:false, base, units:[{size,count,typical_erase_ms}]`，C 侧
   `BL_ERASE_UNIT_TABLE` 显式表，两侧由 test_chip.py 展开比对）、clock、pins、
   iwdg（normal_ms + upgrade_relaxed_ms，**F4 建议 8000 ms**）、sysmem。
   构建段还需声明产物槽位：`artifact_dir`/`sct_dir`（f103c8t6 保持仓库根 legacy 槽位、
   `""`；新芯片用 `chips/<id>` / `linker/<id>`）与 `proj_rel`（uvprojx 相对仓库根的
   深度前缀，根槽位 `""`，子目录槽位 `"..\\..\\"`——Keil 以工程文件所在目录解析全部
   相对路径）。F411CEU6 实例见 `chips/f411ceu6.json`。
4. 实现 `board_config.h`：与 chip.json 四类常量（分区/SRAM/擦除单元/IWDG）保持一致——
   `chips/test_chip.py` 会强制校验两侧。
5. 实现 6 个 ops（§3，Flash 用 unit_* 语义）。
6. 实现 `clock.c` 的 `SystemInit`（§4，**必须包含跳转进入路径**）与 `bl_clock_ops`。
7. 实现 `bl_jump.s`（§6，逐字照搬，只换汇编器语法）。
8. 构建：`CHIP=<id> bash scripts/build_keil.sh`——chipfill 自动从 chip.json+模板生成
   `.spec.json`/`.sct`/工程文件到清单声明的产物槽位，全量构建 0 错 0 警即过（uvprojx 零手工）。
9. 烧录 BL（调试探针，如 `pyocd flash -t <目标> --pack <DFP> bootloader.bin`）→ 用独立
   上位机仓跑 15 步硬件在环检验：`uv run --python 3.12 --with pyserial ../LiteBootUpgrader/bl_upgrade.py selftest --port COM4`（在主仓根目录运行）。
10. 用 `upgrade` 子命令写入一份真 APP → `jump` 验证九步跳转；断电恢复钻具
    （`bl_powerloss_drill.py`，届时为其加 `--chip` 参数化 pyocd 目标）。
11. 按 dev/test_plan.md 验收对照表逐条复核。

## 3. ops 接口实现要求（签名见 port/bl_port.h）

| ops | 关键语义 |
|---|---|
| `bl_flash_ops` | `read` 支持任意地址任意长度；`write` 半字/字对齐、尾部不足自动补 0xFF；**擦除单元**（ADR-015）：`unit_count/unit_addr/unit_size` 暴露单元几何（F4 非均匀扇区按表返回），`erase_unit` 按单元号擦除；`is_range_valid` 做**物理边界防御**（升级路径绝不允许擦写 BL 区与参数区之外） |
| `bl_uart_ops` | `read` 非阻塞（中断 + 环形缓冲），`write` 阻塞发完（响应帧不能截断）；波特率按 `bl_clock_get_hz()` 实测值计算，不要用编译期常量 |
| `bl_gpio_ops` | `write(pin_id, level)`；**引脚映射数据在 board_config.h 声明**（ADR-018：逻辑 id + `*_PORT` 端口序号 + `*_NUM` 引脚号），gpio.c 仅消费——换板改声明即可，chip.json `pins` 段与之强制一致；"低电平点亮"的取反在 UI/APP 层完成，端口层保持语义直白 |
| `bl_wdg_ops` | `init(timeout_ms)` 一次；`set_timeout_ms` 运行时重配（ADR-015 升级期放宽，需先喂狗再改，返回 false 表示不支持/超硬件上限）；`refresh` 极简（仅 KR 写）。IWDG 一旦启动不可关——APP 侧只喂不配 |
| `bl_clock_ops` | `init` 后 `sysclk_hz` 必须与硬件实际一致（§4）；`tick_ms` 供协议超时与 UI 节拍 |
| `bl_i2c_ops` | 仅 BSP 需要；BL 本体不用 |

可直接复用（无需改动）：`core/bl_crc.c`（CRC16/MODBUS、CRC-32/ISO-HDLC，纯计算）、`core/bl_metadata.c`（依赖 `bl_flash` ops 与 `bl_wdg.refresh`，页内布局见 partition.md §4）。

### 3.1 多通道 transport（0.2.0 起，ADR-016）

默认 BL 示例配置为最小集（ADR-019：仅 LED 状态灯 + 串口日志，无蓝牙）。**启用蓝牙
通道（HC-05，transport 通道 1）**：

1. `chips/<id>.json` 的 `build.port_files_bl` 加回 `uart2.c`（与 uart.c 同构：RX 中断 +
   独立环形缓冲 + 两个统计函数 + ops 单例；USART2 类外设注意所在 APB 总线时钟——F1 的
   USART2 挂 APB1 = SYSCLK/2，与 USART1 的 APB2 不同），重新生成工程。
2. `board_config.h` 置 `BL_TRANSPORT_BT_EN=1`，并声明蓝牙引脚（`BL_PIN_BT_STATE/EN`
   及 `*_PORT/_NUM`）与 `BL_BT_UART_BAUD`，`chips/<id>.json` 的 `pins` 同步登记。
3. 通道登记进 `core/bl_transport.c` 的 `s_chans[]`（ops + 统计函数指针）。
4. WIFI 等未实现通道放 `port/wifi_stub.c` 占位（无统计符号，core 自动跳过——真能编译、
   真能被选路识别）。
5. 九步跳转第 6 步经 `bl_port_uart_deinit` 反初始化所有 BL 使用过的 UART：不含某通道的
   目标（如 APP 示例）链接 uart.c 内的 `__weak` 空实现即可，无需链入该通道实现。

**启用 OLED 显示服务（`services/display_oled`，ADR-019 可选能力）**：

1. `chips/<id>.json` 的 `build.service_files` 将 `bl_display_led.c` 替换为
   `bl_display_oled.c`（两者互斥），`bsp_files_bl` 链入 `bsp/oled_ssd1306` 两个源。
2. `port_files_bl` 必须同时加入 `i2c.c`（软件 I2C 端口实现）——显示服务自举
   `bl_i2c.init()`，缺它即 `L6218E: Undefined symbol bl_i2c`（2026-09-30 实测）。
3. 重新生成工程；OLED 状态行的芯片名取自 `board_config.h` 的 `BL_CHIP_NAME`
   （新增 CSP 必须定义该宏）。

**启用签名验签（ADR-020 可选能力，现仅 F411 支持）**：

1. `board_config.h` 置 `BL_SIGN_EN=1`——芯片清单须已登记 `build.sign_files_bl`
   （f411ceu6.json 默认登记；F103 因 BL 16K 预算**不支持**签名，认证能力差异须在
   支持包文档明示）。
2. 本地公钥头 `port/<chip>/bl_sign_pubkey_local.h` 必须就位（由
   `uv run --python 3.12 --with cryptography tools/sign_image.py --keygen`
   生成或按 `docs/dev/bl_sign_pubkey_local.template.h` 手填）——**任何形态不入库**
   （.gitignore 已覆盖）；缺失时编译 `#error`。
3. 全量重建：编译日志**必须出现**启用警告（`#177-D: bl_sign_enabled_notice ...`，
   AC5 无 `#pragma message`/`#warning`，经未引用静态数组实现）——签名变体构建
   退出码 1 属预期；开启态 F411 BL ≈ 18.8 KiB ≤ 32 KiB。
4. 主机侧签名辅助：`tools/sign_image.py --sign <镜像>`（0x11 帧生成）；升级期签名
   集成（`--key`）在 LiteBootUpgrader 后续迭代（密钥对生成器模块一并加入）。
5. HIL 必测：正确签名可跳转 / 篡改 1 字节回 SIGN_ERROR 不持久化 / legacy VERIFY
   镜像 auth=0 拒绝跳转。

## 4. 时钟与 SystemInit——两条进入路径

`SystemInit` 有两种进入方式，**必须区分处理**（F103 实测教训：跳转进入时把 Flash 等待周期降到 0WS，72MHz 下取指损坏，APP 静默硬fault）：

```c
void SystemInit(void)
{
    /* 路径 A：跳转进入（SWS 已是 PLL）——时钟已就绪，确保高等待周期后直接返回，
       严禁落入复位流程（那会把等待周期先降到 0） */
    if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) {
        FLASH->ACR = PRFTBE | 目标等待周期;
        总线分频 = 目标值;
        return;
    }
    /* 路径 B：复位进入——从复位默认时钟（HSI/内部振荡）安全起点开始，
       先保底低等待周期，再启外部振荡/PLL，切换前提升等待周期，失败回退 */
    ...
}
```

- 任何"先降 WS 再升频"的顺序都违反"实际运行频率不得超过等待周期许可达标"原则；等待周期只在切频**前**提升、回退**后**归零。
- `bl_clock_port_init()` 在 main 阶段重读硬件状态（如 SWS 位）派生 `SystemCoreClock`，**不得依赖 SystemInit 传递静态变量**（scatter 清零发生在 `__main`，会覆盖 SystemInit 阶段的全部静态写——阶段 1 实测教训）。

## 5. 启动文件与散布加载

- 启动文件职责：向量表（`RESET` 段导出）、初始 SP、`Reset_Handler → SystemInit → __main`。
- APP 工程（若移植 APP 示例）：scatter 基址 = APP 分区起始；APP 的 `main` 首行设 `SCB->VTOR`、随后 `__enable_irq()`（跳转第 4 步关了全局中断）。
- 初始 SP（向量表首字）必须在 BL 的校验区间内：F103 实现为 `[SRAM_BASE+64, SRAM_BASE+SRAM_SIZE]` 闭区间，栈顶=SRAM 末地址合法。

## 6. 九步跳转的原子性（移植必须逐字遵守）

跳转第 8+9 步（切 MSP + 跳转）**必须用真汇编原子序列**（`bl_jump.s`）：

```asm
bl_port_switch_msp_and_jump        ; r0 = APP 初始 MSP, r1 = APP Reset Handler（Thumb 地址）
        MSR     MSP, r0
        BX      r1
```

**禁止**在 C 函数里调 `__set_MSP` 后继续经 C 返回路径执行：该函数自身的尾声 `POP {r4,pc}` 会从**新栈**（APP 未初始化 RAM）弹出垃圾作为 PC，随机跳址落入 HardFault——F103 实测 fault 现场：栈帧 PC=0（UNDEFINSTR）、LR=该 POP 指令。AC5 内联汇编还禁止 BX/BLX，故此序列必须用独立汇编文件实现。

跳转前序步骤不变：关全局中断 → 停 SysTick → 反初始化外设 → 清 NVIC pending → 设 VTOR → （喂狗已在命令处理层完成）。

## 7. Flash 驱动与参数区适配

- 写入前确保擦除态：F103 用"本会话已擦页位图 + 内容扫描兜底"（bl_storage.c），**同一页多次分块写入不得互相整页擦除**（实测教训：否则页内只剩最后一次写入）。
- 分区必须按擦除单元边界对齐（ADR-015 移植防御，review 2026-09-27 P1）：覆盖 APP 区
  的首/末擦除单元必须与 APP 起止边界精确重合，否则 `bl_storage_init` 拒绝初始化进
  FAULT——防止 F4 非均匀扇区横跨 APP/BL/参数区边界时整单元擦除波及邻区。F1 均匀页
  天然满足；F4 分区设计（BL=扇区 0–1、参数=扇区 2/3、APP=扇区 4 起）天然满足。
- 参数区双副本（partition.md §3-§7）要求**两副本各占一个独立擦除单元**（ADR-015：
  `bl_metadata` 加载期强制校验，F4 两个 16K 扇区直接成立）。副本间隔由
  `BL_PARAM_COPY_SIZE` 定义（F1=1 页，F4=16K 扇区），`chips/<id>.json` 的
  `partitions.params.copies` 与之对应；页内布局与双副本状态机不变。
- `bl_meta_set_bl_request` 等接口的掉电安全性依赖"擦→写→回读校验"三段（bl_metadata.c `write_copy`），移植时保持该顺序并在每段间喂狗。

## 8. 看门狗与喂狗点（跨芯片不变）

喂狗点固定：主循环顶部、ERASE 每擦除单元之间、VERIFY 每 1 KiB 块之间、OLED 帧间、JUMP 执行前（bl_core）。元数据写的擦/写/校三段间也要喂。移植只需保证 `refresh` 足够快（寄存器直写）。

**IWDG 升级期放宽（ADR-015）**：core 在 ERASE_APP 前调 `set_timeout_ms(BL_IWDG_UPGRADE_TIMEOUT_MS)`，VERIFY 完成或跳转前恢复常规值。F4 移植**必须**落实放宽（建议 8000 ms）：单 bank 大扇区（128K 典型 ~875 ms，最大可翻倍）擦除期间 CPU 停顿无法喂狗，2 s 窗口会误复位——这是 F4 移植的头号验收点。F1 取同值 2000 ms（行为等价）。

## 9. 调试器工作流注意（实测）

- pyocd/调试器会话结束会把内核留在停机态（halt-on-connect），表现为串口全静默——**每次会话最后显式复位运行**；排障时先读 `DHCSR` bit17 判断是否停机。
- 用 `verify 回读 CRC 反推 Flash 实际内容`、`fault 现场栈帧（HFSR/CFSR/入栈 PC/LR）解码`、`采样 PC 定位冻结点` 三个手段定位问题（详见 phase 记忆与 dev/test_plan.md §6）。

## 10. 移植验收检查单

- [ ] core/ 零改动（`git diff core/` 为空）
- [ ] 全量构建 0 错 0 警，BL bin ≤ 分区上限
- [ ] selftest 15 步全绿（协议/擦写校/元数据/防护）
- [ ] 真 APP 升级 + 九步跳转 + APP 中断正常（长跑 ≥10 min 无复位）
- [ ] APP 请求回 BL + 复位自动跳转往返
- [ ] 升级中断电（拔电）后可重新升级，BL/参数区完好
- [ ] 文档同步：board_config 常量表、ADR（如有新决策）
