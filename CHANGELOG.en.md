# Changelog

All notable changes to this project are documented in this file. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to
[Semantic Versioning 2.0.0](https://semver.org/).

中文版本：[CHANGELOG.md](CHANGELOG.md)

## [0.2.0] - 2026-09-27

Landing of the "Bluetooth serial + OTA" plan (ADR-016): over-the-air upgrades via
Bluetooth, WIFI API reservation, OTA status query.

### Added

- **Bluetooth transport channel** (goals 1/2): HC-05 (BT 2.0 SPP) attached via USART2
  (PA2/PA3) as transport channel 1 (`port/stm32f1/f103c8t6/uart2.c`), STATE→PB0 /
  EN→PB1 through `bl_gpio_ops`; the module's data-mode baud is configured once via AT
  to 115200 (`BL_BT_UART_BAUD`); online-verified facts recorded in
  [docs/dev/bluetooth_notes.md](docs/dev/bluetooth_notes.md)
- **Multi-channel transport**: `core/bl_transport.c` rewritten as a channel registry
  with active-channel arbitration (2 s idle release, same window as the intra-frame
  byte timeout); the protocol layer switches channels transparently; WIFI is a
  same-signature placeholder stub (`port/wifi_stub.c`, goal 2 — API reservation only,
  implement when actually integrated); also fixes a layering wart (transport no longer
  includes the chip port header directly; stats are declared via bl_port.h)
- **OTA command 0x10 OTA_QUERY** (goal 3, lightweight option confirmed by the user):
  one query returning BL/APP versions, live APP validity, metadata seq, the channel
  the request arrived on, and BT link state; upgrades reuse the existing idempotent
  commands; 0x11–0x1F remain reserved
- OLED status line gained a Bluetooth connection indicator (BT:OK/BT:--); the RX
  diagnostic counter now aggregates all channels

### Changed

- BL version 0.1.0 → 0.2.0; the reserved 0x10–0x1F OTA range is split (0x10 implemented)
- Doc sync: architecture §6.1 (channel registry & arbitration), protocol
  §3/§4.3/§5.10/§7.5, external_interface §1.1/§1.4/§3.1/§6, design ADR-016,
  porting_guide §3.1, partition §1 (OTA boundary is channel-agnostic), user manual
  Bluetooth section
- Host tool counterpart: LiteBootUpgrader v1.3.0 (`ota` subcommand, `--conn bt`,
  GUI connection type / OTA query)

### Verification

- AC5 full rebuild with 0 errors / 0 warnings: BL 15,324 B ≤ 16 KiB (SHA-256
  `499de4bc…`), APP 8,152 B ≤ 46 KiB (SHA-256 `534eb656…`); `chips/test_chip.py`
  11/11 passed
- Bluetooth hardware-in-the-loop (real HC-05 upgrade, disconnect recovery, dual-channel
  interference) **not verified** — requires the target board, follow the "Bluetooth
  upgrade" section of [docs/user_manual.md](docs/user_manual.md)

## [0.1.0] - 2026-09-27

First public release.

### Added

- **BootLoader core** (`core/`): upgrade protocol state machine and command handling,
  flash storage with erase-unit abstraction, dual-copy parameter metadata (power-loss
  safe), boot decision, APP image validation and atomic jump
- **STM32F103C8T6 support package** (`port/stm32f1/f103c8t6/`): flash / uart / i2c /
  gpio / wdg / clock / systick implementations, 72 MHz (HSE 8 MHz + PLL, HSI fallback)
- **Multi-chip infrastructure (CSP, ADR-015)**: `chips/<id>.json` chip manifests
  (build-side source of truth), template-driven spec/sct/project generation (LiteTools
  chipfill), erase-unit abstraction, parameterized IWDG relaxation during upgrades
- **Example APP** (`app/examples/f103c8t6_app/`): breathing LED + active request to enter BL
- **Service layer** (`services/`): OLED+LED display service, USART1 log service
  (leveled, compile-time switchable)
- **External tool repos**: [LiteBootUpgrader](https://github.com/Liu-bit264/LiteBootUpgrader)
  (host tool CLI + GUI), [LiteTools](https://github.com/Liu-bit264/LiteTools)
  (Keil uvprojx / ICO tools, chipfill CSP filler)
- **Scripts**: toolchain check (`check_toolchain.sh|.bat`), one-shot CSP build (`build_keil.sh`)
- **Documentation**: user manual, architecture, protocol specification (protocol
  contract), partition & dual-copy state machine, external interfaces, porting guide,
  test plan, design decision records (`docs/dev/`)

### Verification

- Full hardware validation on F103C8T6: 14/14 acceptance items passed, host-tool
  upgrade E2E, power-loss recovery drills
- Artifact baseline: BL 13,728 B (SHA-256 `ce41b1b6…`), APP 8,072 B (SHA-256 `1ab705d6…`)

### Notes

- Development history before 0.1.0 is available via `git log` (no version tags were cut
  during that period)
- As of 0.1.0 the host tool and project tools live in separate repositories (links
  above); this repo no longer contains their code
