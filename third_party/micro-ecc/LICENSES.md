# third_party/micro-ecc 来源与许可

本目录**原样拷贝**自上游 micro-ecc 仓库（未修改任何代码），来源与许可如下：

- **上游**：https://github.com/kmackay/micro-ecc @ commit `541b3a78026420a3e369c4c9281c396b5e531113`（2024-11-13，master）
- **许可**：BSD-2-Clause（`LICENSE.txt` 原文随目录捆绑）

## 捆绑文件（8 个）

| 文件 | SHA-256（如捆绑） |
|---|---|
| `uECC.c` | `e9db3217c2e8aacb2e1930004a9236b7502d4f0d44fd5796f45e3923f61eaeba` |
| `uECC.h` | `9d480aa6920155f68503064bebefb815a82aab318ab305efdcf04674d2a9cba7` |
| `uECC_vli.h` | `a8ff7957855e6520674cad4bcaf8f3e752d40f6c3f7501915c3504c777be1e7a` |
| `types.h` | `61bd255c0dca4c69dfc779cc5c1fe1cbff47039d4bb7f37578a88f63b7fca466` |
| `platform-specific.inc` | `1f30c1032d3545b4b74e9cb66cdcd33a715c3fd435332c3016e75cae443146e0` |
| `curve-specific.inc` | `27e0ce3309c80f8f8d6a74448223072d17b0bb90a501cfbdd68a12c1de07f08a` |
| `LICENSE.txt` | `ffd8b033d2df7568c25a98866bd92b1656f7da82d7f813c0b2eb85ec36611193` |
| `README.md` | `6dc943ae31e374dc0307f296dc4fe5a636858a4860c1cae309388a7c68f9ae1e` |

## 原样性说明（2026-09-30）

- 内容与上游 commit `541b3a7` **逐字符一致**，仅两类**空白级**归一化（无任何 token 变化，
  `diff --ignore-all-space` 与上游工作区副本零差异；哈希为归一化后本仓快照）：
  1. 行尾统一为 LF（上游仓库对象即 LF；CRLF 仅为本机 git autocrlf 检出产物）；
  2. 上游部分空行的行尾空格（`"    "`）与两处代码行行尾空格未保留。
- 未捆绑：`asm_*.inc`（ARM/AVR 汇编加速，仅 `uECC_PLATFORM` 探测为 arm/avr 时被
  `uECC.c` include——本项目以编译定义强制 `uECC_PLATFORM=0`（arch_other）走纯 C
  路径，见下）、`test/`、`examples/`、`emk_*.py`、`library.properties`（构建系统与
  测试夹具，与固件构建无关）。

## 本项目编译配置（ADR-020）

uECC 行为经**编译定义**配置（`chips/f411ceu6.json` `build.c_defines`，随 uvprojx 注入，
不改源文件）：

| 定义 | 值 | 理由 |
|---|---|---|
| `uECC_PLATFORM` | `0`（uECC_arch_other） | AC5 定义 `__arm__` 会被 types.h 探测为 ARM 平台并要求 asm_*.inc；强制 arch_other 走纯 C，免去汇编捆绑 |
| `uECC_WORD_SIZE` | `4` | Cortex-M3/M4 32 位 |
| `uECC_SUPPORTS_secp160r1/192r1/224r1/256k1` | `0` | 仅保留 secp256r1（ADR-020 选型），缩代码 |
| `uECC_SUPPORT_COMPRESSED_POINT` | `0` | 固件只收 64B 非压缩公钥，砍 mod_sqrt |

- `uECC_OPTIMIZATION_LEVEL` 保持上游默认 2；`uECC_VLI_NATIVE_LITTLE_ENDIAN` 保持默认 0
  ——此时公钥/签名字节序为**标准大端**（`uECC_vli_bytesToNative` 语义），与主机侧
  cryptography/openssl 的 r‖s 大端裸序直接互操作。
- 裸机环境无 RNG：`default_RNG` 不定义，`g_rng_function = 0`——`uECC_verify` 不用
  RNG，满足验签用途；禁用 `uECC_make_key/uECC_sign`（固件侧只验签）。
- 本目录仅 BL 目标引用（`chips/f411ceu6.json` `build.sign_files_bl`）；F103 不启用
  签名（BL 16K 预算），其构建清单不含本目录文件。
