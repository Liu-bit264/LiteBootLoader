# VOFA+ 配置指南（LiteBootLoader）

> 本目录存放 VOFA+ 相关交付物。当前包含：
> - [rawdata_frames.md](rawdata_frames.md)：全部命令的 RawData 十六进制帧模板（CRC 已预计算，可直接复制发送）。

## 定位声明（重要）

VOFA+ 在本项目中的定位是**日志观察 + RawData 手动发帧的调试辅助**，不是正式升级器：

| 能做 | 不能做 |
|---|---|
| 观察 BL/APP 串口日志（升级横幅、心跳、状态） | 自动分块写入（WRITE_CHUNK 需逐块算 offset 与 CRC） |
| 手动发送单条命令（PING / ERASE / JUMP 等） | 自动校验 VERIFY（需预知镜像 size + CRC32） |
| 直观验证帧格式与响应 | 原子化"请求回 BL → 擦 → 写 → 校验 → 跳转"全流程 |

正式升级请使用独立上位机仓 `../../LiteBootUpgrader/bl_upgrade.py`（见 [docs/protocol.md](../../docs/protocol.md)）。
两者共用同一 USART1（COM 口互斥：VOFA+ 打开串口期间 Python 工具无法使用，反之亦然）。

## 本机安装位置

```
E:\hw-tools\assist\hosts\uart\x64\vofa+.exe
```

（截至 2026-09-25 该目录从未运行过 VOFA+，无历史配置；以下为首跑步骤。）

## 首跑配置步骤

> 以下操作流程已对照 VOFA+ 官方文档（vofa.plus）核实：协议引擎三种
> （RawData / FireWater / JustFloat），官方对 RawData 的定位原话为
> "如果您只把 VOFA+ 当成串口调试助手……请务必使用本协议"；
> 发送用「命令」控件，支持 Str(Ascii)/Hex 两种模式；接收侧原始字节流
> 用「视图 → 原始数据视图」的 hex/ASCII 双栏窗口查看。

1. **启动**：双击 `vofa+.exe`。
2. **选择引擎**：连接区选择 **RawData** 引擎（不是 JustFloat / FireWater——
   那两个是采样数据/波形解析协议；RawData 对收发字节"一五一十打印"，不做解析）。
3. **打开串口**：
   - 端口：设备管理器中 DAPLink CDC 对应的 COM 号（本机为 COM4，以实际为准）
   - 波特率：**115200**
   - 数据位 8 / 停止位 1 / 无校验 / 无流控
   - 点击连接。
4. **配置发送命令**：左侧栏添加「命令」控件，模式切换为 **Hex**，
   把 [rawdata_frames.md](rawdata_frames.md) 的帧十六进制粘进命令内容
   （可添加多条命令，保存/载入，绑定按钮点击发送）。
5. **配置接收观察**：顶部菜单 **视图 → 原始数据视图**，打开 hex/ASCII
   双栏窗口，实时查看收到的原始字节（响应帧为二进制，以此窗口比对为准）。
6. **快速自检**：发送 PING 帧，应收到
   `AA 55 81 01 01 02 00 <CRC_L> <CRC_H> 55 AA`（DATA = `00 01`，即 OK + 协议版本 0x01）。

## 与 BL 的交互注意

- BL 帧内字节间隔超时 2000 ms：手动发送同一帧时一次性粘贴发送，不要拆成多次间隔发送，
  否则 BL 会在 2 s 后丢弃半帧重新同步。
- 发送坏 CRC 帧会被 BL 静默丢弃（无响应），用于验证 CRC 防护时可据此判定。
- 日志与协议共用 USART1：升级会话（最近 10 s 内有合法帧）期间 BL 自动静音日志，
  RawData 手动调试时看到日志"消失"属预期行为。
- ERASE_APP 全 46 页擦除约 1~2 s，请耐心等待响应；期间 IWDG 由 BL 内部喂狗，不会复位。
