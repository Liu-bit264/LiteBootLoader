# Changelog

All notable changes to this project are documented in this file. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to
[Semantic Versioning 2.0.0](https://semver.org/).

中文版本：[CHANGELOG.md](CHANGELOG.md)

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
