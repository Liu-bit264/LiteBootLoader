# 版本与提交规范（versioning）

> 版本 0.1.0 · 2026-09-25 · 状态：阶段 0 交付
> 上位规则：AGENTS.md §11（SemVer 2.0.0 + Conventional Commits 1.0.0），本文为其操作细则。

## 1. 语义化版本（SemVer 2.0.0）

- 格式 `MAJOR.MINOR.PATCH`，Git tag `vX.Y.Z`。
- `fix` → PATCH；`feat` → MINOR；`BREAKING CHANGE`（提交脚注或 `!`）→ MAJOR。
- **当前版本 `0.1.0`**（阶段 0：设计与基础设施）。`0.x` 阶段允许 MINOR 内包含破坏性调整；`1.0.0` 于首次硬件在环验收通过后打标。

## 2. BL 固件版本（`core/bl_version.h`）

```c
#define BL_VERSION_MAJOR 0
#define BL_VERSION_MINOR 1
#define BL_VERSION_PATCH 0
#define BL_VERSION_STRING "0.1.0"
```

规则：

- 宏为唯一事实来源，`GET_INFO` 响应（protocol.md §5.2）与 OLED 显示（architecture.md §8）均从此取值。
- 发版流程：改宏 → 构建产物 SHA-256 记录到验收记录 → tag `vX.Y.Z`（tag 由用户确认后执行）。
- 版本递增映射 Conventional Commits 类型（§3）。

## 3. Conventional Commits 1.0.0

格式：`<type>(<scope>): <subject>`，破坏性变更用 `!` 或脚注 `BREAKING CHANGE:`。

| type | 含义 | 版本影响 |
|---|---|---|
| `feat` | 新功能 | MINOR |
| `fix` | 缺陷修复 | PATCH |
| `docs` | 文档 | 无（除非文档即交付物，按约定 PATCH） |
| `style` | 格式（不影响语义） | 无 |
| `refactor` | 重构 | 无 |
| `perf` | 性能 | PATCH |
| `test` | 测试 | 无 |
| `chore` | 构建/工具/杂项 | 无 |
| `port` | 芯片端口移植 | 按 feat/fix 性质判定 |

scope 取值（新增需先在本文登记）：`core`、`port/f1`、`port/f4`、`port/g0`、`port/h7`、`protocol`、`ui`、`bsp/oled`、`tools`、`tools/uvprojx`、`tools/ico`、`docs`、`scripts`、`partition`。

示例：

```text
feat(protocol): 实现 WRITE_CHUNK 自动擦页与喂狗点
fix(port/f1): HSE 失败回退 HSI 后重算 USART 波特率
docs(partition): 补充 seq 回绕规则
```

## 4. 协议版本独立演进

- 升级帧的 `VER` 字节（protocol.md §4）独立于 BL 固件版本：**协议字段布局变更 = VER 递增**。
- 兼容规则：BL 至少支持当前 VER 与前一 VER 的解析（v1 阶段仅 0x01，发现非当前版本一律丢弃重新同步）；工具侧首发帧前先 PING 探测 `protocol_ver`。
- 协议破坏性变更（改字段含义/布局）必须：VER+1 + protocol.md 变更记录 + 工具与 BL 同步发版。

## 5. 文档版本

本文档集（docs/*.md）随固件走同一版本号；每份文档头部带自身版本与日期，变更在对应文档头部更新。文档与实现不一致时，按 `docs` 类型提交修正并注明影响范围。
