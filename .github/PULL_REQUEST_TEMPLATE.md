## 改动说明

<!-- 做了什么、为什么。涉及协议 / 分区 / 接口 / 默认假设时，注明文档同步情况 -->

## 变更类型

- [ ] `fix`（bug 修复 → PATCH）
- [ ] `feat`（新功能 → MINOR）
- [ ] `port`（芯片支持包 → 视性质判定）
- [ ] `docs` / `refactor` / `test` / `chore` / `perf` / `style`

## 自查清单

- [ ] 提交信息符合 Conventional Commits（docs/dev/versioning.md）
- [ ] 构建通过（UV4 退出码 0 或 1，按退出码判定；附日志）
- [ ] 相关测试通过（视涉及面）：`chips/test_chip.py` / LiteTools 单测 /
      LiteBootUpgrader `test_host_protocol.py`
- [ ] 硬件回归（涉及固件行为时：烧录 → 升级 → 校验 → 跳转 → 断电恢复；
      无板则注明「未验证」及原因）
- [ ] 文档同步（视涉及面）：protocol.md / partition.md / architecture.md /
      external_interface.md / CHANGELOG.md
- [ ] 未引入动态内存；core 未引入 HAL 依赖；未散落硬编码引脚/时钟/分区/看门狗参数

## 硬件验证证据（如有）

<!-- 构建尺寸、SHA-256、测试命令与输出摘要 -->
