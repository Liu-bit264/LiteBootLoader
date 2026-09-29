# third_party/CMSIS 来源与许可

本目录头文件**原样拷贝**自本机文件（未做任何修改），来源与许可如下：

| 文件 | 来源 | 许可 |
|---|---|---|
| `core_cm3.h`（CMSIS V1.30）`stm32f10x.h`（V3.5）`system_stm32f10x.h` | 本机已验证工程 `E:\hw-tools\projects\git\embedded\STM32F103\1_LIghtUp\Start\`（ST StdPeriph V3.5 时代配套，2011） | ST/ARM 旧版许可（允许随项目再分发，需保留版权声明；文件头部含原文） |
| `core_cm4.h` `cmsis_compiler.h` `cmsis_armcc.h` `cmsis_version.h` `mpu_armv7.h`（CMSIS Core(M) V5.6.0，Arm © 2009-2020） | 本机 `E:\hw-tools\em-tools\STMRepo\STM32Cube_FW_F4_V1.28.3\Drivers\CMSIS\Core\Include\`（ST 官方 F4 固件包） | Apache-2.0（文件头部含原文与 SPDX 标识） |
| `stm32f4xx.h` `stm32f411xe.h` `system_stm32f4xx.h`（CMSIS Device STM32F4xx，ST © 2017-2023） | 本机 `E:\hw-tools\em-tools\STMRepo\STM32Cube_FW_F4_V1.28.3\Drivers\CMSIS\Device\ST\STM32F4xx\Include\` | Apache-2.0（器件头指向的 LICENSE.txt：包内按 Package_license 生效，否则 Apache-2.0；`Package_license.md` 列 CMSIS Device = Apache-2.0） |

选型说明：

- **本项目固定使用 AC5（ARMCC V5.06 update 7）**（design.md ADR-013，用户决定：兼容性优先）。
- CMSIS 6（pack 6.1.0）已移除 AC5 支持（无 cmsis_armcc.h，compiler 头仅认 AC6），因此内核头改用 V1.30 自包含版——与本机 AC5 组合已在该工程实际编译验证。
- **F4（M4 内核）用 CMSIS Core(M) V5.6.0**：该版本保留 `cmsis_armcc.h`（AC5 分支仍可用），CubeFW F4 V1.28.3 随包发行、与本机 AC5 组合经本仓 F411CEU6 支持包全量重建验证；F1 继续用 V1.30（改动会牵动已验证的 F103 基线，不动）。
- 此组合下 F1 无需 m-profile/cmsis_compiler 等拆分头；F4 头包含链为
  `stm32f4xx.h → stm32f411xe.h → core_cm4.h → cmsis_compiler.h → cmsis_armcc.h`
  （另含 `cmsis_version.h`、`mpu_armv7.h`、`system_stm32f4xx.h`），家族边界由
  chip.json 的 `build.cmsis_sources`/`c_defines` 表达（F1: `STM32F10X_MD`，F4: `STM32F411xE`）。
- 如未来迁移 AC6，需同步更换两侧 compiler 头并更新本说明。

## 原样性核验（2026-09-27）

上表 F1 侧 4 个文件已与源工程 `E:\hw-tools\projects\git\embedded\STM32F103\1_LIghtUp\Start\` 中的副本逐字节比对（`diff` 无差异，`diff -q` 判定 IDENTICAL），确认**原样拷贝、未做任何修改**。文件 SHA-256：

| 文件 | SHA-256 |
|---|---|
| `core_cm3.c` | `e93a7e36349e7810f328276a3fed56701e10536779f7527a8ccd140618a556b0` |
| `core_cm3.h` | `de4668aa9d314d05769dfb29f028e205b50577c6b2150fc6bccb34bd34179bbb` |
| `stm32f10x.h` | `4ee722d262c1e7d7505d7dc7eeb8ccaaf67158b51ee4bc177397cd64e74e1b3a` |
| `system_stm32f10x.h` | `919dc1f5c6bbba74cb712e8ec7cf73f1988eafa9438c706526fb401a2ab4672d` |

## 原样性核验（2026-09-29，F4 侧）

F4 侧 8 个文件经 `scripts/vendor_copy.py` 自 STM32Cube_FW_F4_V1.28.3 拷入，**逐字节核验 IDENTICAL**（拷贝后读回 SHA-256 与源一致）：

| 文件 | SHA-256 |
|---|---|
| `core_cm4.h` | `f5b63d52dd1557b15ca414cb59c264f595a7b5669db2a5878dffe22f4caedc8c` |
| `cmsis_compiler.h` | `b51963d271c1571ca3463654c76aa7ea50c4eac5d0a2710df10414ac64d9e0f8` |
| `cmsis_armcc.h` | `5bc0d7ed8e36e9bcb868c4042fff350c702eca305df4162fb00384789e35ee1c` |
| `cmsis_version.h` | `184c19fd3ee73632edf35a0b4d49cd48be75fbf49e6ccb19d9db05fa83bea4b3` |
| `mpu_armv7.h` | `29206b52ee02290ed6f5a5415ebd4187de802cf176d9b3cb844390d8e5571372` |
| `stm32f4xx.h` | `a19edea6b2f4df2ab3c567782c4d313a52319c4303bd56ad7dd1d0e077014657` |
| `stm32f411xe.h` | `fc97c0ca8982da2139064fb0e97c7c60d5473edee354170912007341dc822f43` |
| `system_stm32f4xx.h` | `02c067d5a135f540c03215dc834b1e60a97bbd4e55bfd3994b0c82a15ea3012a` |

注：`startup_stm32f411xe.s` 未入本目录——它是**改编件**（去堆、统一注释），按项目惯例
放在 `port/stm32f4/f411ceu6/`（与 F1 `startup_stm32f10x_md.s` 同一处置），原始出处见该文件头注。
