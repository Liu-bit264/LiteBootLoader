# 移植指南（Porting Guide）

> 目标读者：要把本 BootLoader 框架迁移到其它芯片（F4 / G0 / H7 等）的工程师。
> 原则：**core 层一行不改**，所有芯片差异收敛到 `port/<家族>/<型号>/` 与 `board_config.h`。
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
- `third_party/CMSIS` 按目标芯片更换；注意编译器支持（本项目用 AC5，CMSIS 6 已弃 AC5，故使用 V1.30 自包含内核头——见 third_party/CMSIS/LICENSES.md）。

## 2. 移植步骤清单（新家族接入顺序）

1. 建 `port/<家族>/<型号>/`，复制 F103 实现作为骨架。
2. 换启动文件（向量表、堆栈）与 CMSIS 器件头；确认 `RESET` 段仍被 scatter 以 `*.o (RESET, +First)` 置于镜像首。
3. 实现 `board_config.h`：分区表（BL/APP/参数区，页大小）、SRAM 范围、IWDG 超时、UART 波特率、引脚编号。
4. 实现 6 个 ops（§3）。
5. 实现 `clock.c` 的 `SystemInit`（§4，**必须包含跳转进入路径**）与 `bl_clock_ops`。
6. 实现 `bl_jump.s`（§6，逐字照搬，只换汇编器语法）。
7. 更新 `*.spec.json`（uvprojx 生成器输入）→ 生成工程 → 全量构建 0 错 0 警。
8. 烧录 BL → 用 `tools/python/bl_upgrade.py selftest` 跑 15 步硬件在环检验。
9. 用 `upgrade` 子命令写入一份真 APP → `jump` 验证九步跳转。
10. 按 test_plan.md 验收对照表逐条复核。

## 3. ops 接口实现要求（签名见 port/bl_port.h）

| ops | 关键语义 |
|---|---|
| `bl_flash_ops` | `read` 支持任意地址任意长度；`write` 半字/字对齐、尾部不足自动补 0xFF；`erase_page` 按页号；`is_range_valid` 做**物理边界防御**（升级路径绝不允许擦写 BL 区与参数区之外）；`page_size` 供 core 分块与位图计算 |
| `bl_uart_ops` | `read` 非阻塞（中断 + 环形缓冲），`write` 阻塞发完（响应帧不能截断）；波特率按 `bl_clock_get_hz()` 实测值计算，不要用编译期常量 |
| `bl_gpio_ops` | `write(pin_id, level)`；"低电平点亮"的取反在 UI/APP 层完成，端口层保持语义直白 |
| `bl_wdg_ops` | `init(timeout_ms)` 一次；`refresh` 极简（仅 KR 写）。IWDG 一旦启动不可关——APP 侧只喂不配 |
| `bl_clock_ops` | `init` 后 `sysclk_hz` 必须与硬件实际一致（§4）；`tick_ms` 供协议超时与 UI 节拍 |
| `bl_i2c_ops` | 仅 BSP 需要；BL 本体不用 |

可直接复用（无需改动）：`core/bl_crc.c`（CRC16/MODBUS、CRC-32/ISO-HDLC，纯计算）、`core/bl_metadata.c`（依赖 `bl_flash` ops 与 `bl_wdg.refresh`，页内布局见 partition.md §4）。

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
- 参数区双副本（partition.md §3-§7）假设**两副本各占一个等大页**。F103（1 KiB 均匀页）直接成立；扇区不等大的芯片（F4 等）需为参数区单独规划：预留独立小扇区，或改用片内备份域/外部存储——`bl_metadata.c` 的页内布局与双副本状态机不变，仅页地址/大小来源需参数化。
- `bl_meta_set_bl_request` 等接口的掉电安全性依赖"擦→写→回读校验"三段（bl_metadata.c `write_copy`），移植时保持该顺序并在每段间喂狗。

## 8. 看门狗与喂狗点（跨芯片不变）

喂狗点固定：主循环顶部、ERASE 每页之间、VERIFY 每 1 KiB 块之间、OLED 帧间、JUMP 执行前（bl_core）。元数据写的擦/写/校三段间也要喂。移植只需保证 `refresh` 足够快（寄存器直写）。

## 9. 调试器工作流注意（实测）

- pyocd/调试器会话结束会把内核留在停机态（halt-on-connect），表现为串口全静默——**每次会话最后显式复位运行**；排障时先读 `DHCSR` bit17 判断是否停机。
- 用 `verify 回读 CRC 反推 Flash 实际内容`、`fault 现场栈帧（HFSR/CFSR/入栈 PC/LR）解码`、`采样 PC 定位冻结点` 三个手段定位问题（详见 phase 记忆与 test_plan.md §6）。

## 10. 移植验收检查单

- [ ] core/ 零改动（`git diff core/` 为空）
- [ ] 全量构建 0 错 0 警，BL bin ≤ 分区上限
- [ ] selftest 15 步全绿（协议/擦写校/元数据/防护）
- [ ] 真 APP 升级 + 九步跳转 + APP 中断正常（长跑 ≥10 min 无复位）
- [ ] APP 请求回 BL + 复位自动跳转往返
- [ ] 升级中断电（拔电）后可重新升级，BL/参数区完好
- [ ] 文档同步：board_config 常量表、ADR（如有新决策）
