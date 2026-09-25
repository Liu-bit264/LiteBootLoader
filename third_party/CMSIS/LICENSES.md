# third_party/CMSIS 来源与许可

本目录头文件**原样拷贝**自本机文件（未做任何修改），来源与许可如下：

| 文件 | 来源 | 许可 |
|---|---|---|
| `core_cm3.h`（CMSIS V1.30）`stm32f10x.h`（V3.5）`system_stm32f10x.h` | 本机已验证工程 `E:\hw-tools\projects\git\embedded\STM32F103\1_LIghtUp\Start\`（ST StdPeriph V3.5 时代配套，2011） | ST/ARM 旧版许可（允许随项目再分发，需保留版权声明；文件头部含原文） |

选型说明：

- **本项目固定使用 AC5（ARMCC V5.06 update 7）**（design.md ADR-013，用户决定：兼容性优先）。
- CMSIS 6（pack 6.1.0）已移除 AC5 支持（无 cmsis_armcc.h，compiler 头仅认 AC6），因此内核头改用 V1.30 自包含版——与本机 AC5 组合已在该工程实际编译验证。
- 此组合下无需 m-profile/cmsis_compiler 等拆分头；如未来迁移 AC6，需换回 CMSIS 6 头并同步更新本说明。
