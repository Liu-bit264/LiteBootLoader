# LiteBootLoader — BootLoader Framework for STM32

[简体中文](README.md) | English

A compilable, testable and portable BootLoader (BL) framework for STM32:
boot decision, APP image validation, dual-channel upgrade protocol (USART1 wired +
Bluetooth HC-05), OTA status query, flash and parameter-area management, OLED/LED
status display, IWDG watchdog and safe jump-to-APP.
Multi-chip porting is supported through the core/port layering and **Chip Support
Packages (CSP: `chips/*.json` + `port/<family>/<chip>/`)**.

- **User manual (start here for usage)**: [docs/user_manual.md](docs/user_manual.md)
- **Feedback & contributing**: [CONTRIBUTING.md](CONTRIBUTING.md) · Changelog: [CHANGELOG.en.md](CHANGELOG.en.md)

## Current Status

- **Current support package: STM32F103C8T6** (Cortex-M3, 64 KiB flash / 20 KiB RAM) —
  the only support package with full hardware validation so far: 14/14 acceptance items
  passed, host-tool upgrade E2E, power-loss recovery drills; artifact sizes and SHA-256
  are recorded in the [changelog](CHANGELOG.en.md)
- **Bluetooth over-the-air upgrades (0.2.0)**: HC-05 (SPP) attached via UART2 as
  transport channel 1, WIFI reserved at the API level; OTA status query command 0x10
  (ADR-016); on-target Bluetooth validation is in progress
- STM32F4 / G0 / H7 port directories are reserved (skeletons only); the full workflow for
  adding a new chip is described in [docs/porting_guide.md](docs/porting_guide.md)
- Multi-chip infrastructure (CSP: chip manifest + template-driven project generation +
  erase-unit abstraction + parameterized IWDG) is in place — see
  [docs/dev/design.md](docs/dev/design.md) ADR-015

## Repository Layout

| Directory | Purpose |
|---|---|
| `core/` | State machines, protocol, boot policy, metadata, common logic (no HAL dependency) |
| `services/` | Display (OLED+LED) and debug (USART1 log) services |
| `chips/` | CSP chip manifests (build-side source of truth) + spec templates + consistency tests |
| `port/` | Chip port layer (`stm32f1/f103c8t6` implemented; `stm32f4/g0/h7` reserved) |
| `bsp/` | Board-level device drivers (`oled_ssd1306`, etc.) |
| `app/examples/` | Example APP project (linked at `0x08004000`) |
| `linker/` | Linker scripts / scatter files (`*.sct` are generated artifacts) |
| `tools/` | VOFA+ debug frame templates (uvprojx/ico project tools live in the standalone [LiteTools](../LiteTools) repo) |
| `docs/` | Architecture / protocol / partition / interfaces / user manual / porting guide (`docs/dev/` holds development & agent docs) |
| `scripts/` | Toolchain checks and build scripts |
| `third_party/` | CMSIS / HAL dependencies (bundled verbatim, see `third_party/CMSIS/LICENSES.md`) |

## Quick Start

```bash
# 1) Toolchain check (Git Bash / Linux; CMD: scripts\check_toolchain.bat)
bash scripts/check_toolchain.sh

# 2) Keil project parsing / generation (tools live in the sibling repo ../LiteTools)
python ../LiteTools/uvprojx/parser.py <project.uvprojx> -o spec.json
python ../LiteTools/uvprojx/generator.py spec.json -o new.uvprojx        # create mode (verify the result in Keil manually)
python ../LiteTools/uvprojx/generator.py spec.json --update old.uvprojx  # update mode (keeps unknown fields, auto-backup)

# 3) ICO icon parsing / generation (also in ../LiteTools)
python ../LiteTools/ico/parser.py <file.ico>
python ../LiteTools/ico/generator.py --sizes 16,32,48,256 -o icon.ico icon.png
```

> **Dependency isolation (recommended)**: run Python tools in an isolated environment
> (`uv run --with <pkg> <script>` or a venv with pyserial/Pillow installed) instead of
> polluting the global interpreter. The examples below use `uv`; plain `python` works as
> long as the dependencies are installed in the active environment.

## Building (CSP flow)

```bash
# One-shot build (chip.json+templates -> spec/sct -> generated project -> UV4 full rebuild
# -> exit-code check -> bin extraction). CHIP selects the chip manifest (default f103c8t6);
# TARGETS defaults to "bootloader app".
CHIP=f103c8t6 bash scripts/build_keil.sh

# Manual equivalent:
python ../LiteTools/uvprojx/chipfill.py --chip chips/f103c8t6.json --target bootloader \
    --spec-out bootloader.spec.json --sct-out linker/bootloader.sct   # manifest -> spec & scatter
python ../LiteTools/uvprojx/generator.py bootloader.spec.json -o bootloader.uvprojx  # spec -> project
"<Keil install dir>/UV4/UV4.exe" -r bootloader.uvprojx -j0 -o keil_build.log
# Exit codes: 0=no warnings/errors, 1=warnings, >=2=errors
"<Keil install dir>/ARM/ARMCC/bin/fromelf.exe" --bin --output=bootloader.bin Objects/bootloader.axf

# After changing chips/<id>.json or templates: regenerate all artifacts (test_chip.py enforces round-trip)
python chips/test_chip.py

# Upgrade the APP over the BL protocol and jump (host tool in the sibling repo ../LiteBootUpgrader;
# usage documented in docs/protocol.md)
uv run --with pyserial ../LiteBootUpgrader/bl_upgrade.py upgrade app/examples/f103c8t6_app/app.bin --port COMx
uv run --with pyserial ../LiteBootUpgrader/bl_upgrade.py jump --port COMx
```

> The compiler is pinned to **AC5** (V5.06u7, ADR-013); CMSIS core headers are V1.30
> (see `third_party/CMSIS/LICENSES.md`). Replace `<Keil install dir>` with your local
> installation (typically `C:\Keil_v5` on Windows), or point the `KEIL_UV4` environment
> variable at UV4.exe.

## Three-Repo Layout & Coupling

| Repository | Purpose | Coupling contract & sync rule |
|---|---|---|
| **LiteBootLoader** (this repo) | Firmware + protocol contract | `docs/protocol.md` is the single protocol specification; protocol/partition/jump behavior changes update the docs and bump the version first |
| [LiteBootUpgrader](../LiteBootUpgrader) | Host tool CLI + GUI (serial upgrade / jump / self-test) | Implements the current protocol.md version (VER 0x01); after protocol or behavior changes here, LBU must sync and pass `test_host_protocol.py` plus hardware E2E regression |
| [LiteTools](../LiteTools) | Keil uvprojx / ICO tools + chipfill | Consumes this repo's `chips/<id>.json` schema and `chips/templates/` spec templates; schema or template changes require LiteTools unit tests + this repo's `chips/test_chip.py` regression |

## Documentation

| Document | Contents |
|---|---|
| [docs/user_manual.md](docs/user_manual.md) | **User manual**: wiring, first flash, daily upgrades, indicators, troubleshooting |
| [docs/architecture.md](docs/architecture.md) | Layered architecture, ops interfaces, runtime model, state machines, memory budget |
| [docs/protocol.md](docs/protocol.md) | Protocol specification (frame format, commands, example frames, tool usage) — single protocol contract |
| [docs/partition.md](docs/partition.md) | Flash partitions, dual-copy parameter area state machine, power-loss recovery |
| [docs/external_interface.md](docs/external_interface.md) | External interface list (pins, protocol summary, abstract interfaces, OTA extension points) |
| [docs/porting_guide.md](docs/porting_guide.md) | Porting guide: ops requirements, dual clock path, atomic jump, pitfalls |
| [docs/dev/bluetooth_notes.md](docs/dev/bluetooth_notes.md) | Bluetooth HC-05 verified facts: pin semantics, one-time AT setup, sources |
| [docs/dev/design.md](docs/dev/design.md) | Design & decision records (CRC parameters, upgrade modes, LED/log policies, ADRs) |
| [docs/dev/versioning.md](docs/dev/versioning.md) | SemVer & Conventional Commits rules |
| [docs/dev/vofa_plus.md](docs/dev/vofa_plus.md) | VOFA+ positioning and feasibility notes |
| [docs/dev/test_plan.md](docs/dev/test_plan.md) | Test plan: four-level testing, selftest list, acceptance matrix, lessons learned |

## Feedback & Contributing

- Bugs and chip-support requests: GitHub Issues (`bug_report` and `chip_support` templates provided)
- Contributing workflow, CSP PR checklist and quality baseline: [CONTRIBUTING.md](CONTRIBUTING.md)
- Working rules for coding agents: [AGENTS.md](AGENTS.md)

## License

Original code of this project (`core/`, `port/`, `services/`, `bsp/`, `app/`, `linker/`, `tools/`, `scripts/`, `docs/`) is released under the [MIT License](LICENSE) (© 2026 Qingc).

Third-party files bundled under `third_party/` are **verbatim copies** (unmodified) and retain their original licenses and copyright notices; sources, license terms and byte-level verification records are documented in [third_party/CMSIS/LICENSES.md](third_party/CMSIS/LICENSES.md).
