# LiteBootLoader — BootLoader Framework for STM32

[简体中文](README.md) | English

A compilable, testable and portable BootLoader (BL) framework for STM32: boot decision,
APP image validation, an upgrade protocol with OTA status query (wired USART1 is the
mandatory channel, Bluetooth HC-05 an optional capability), flash and parameter-area
management, status display (LED status by default, OLED optional), IWDG watchdog and
safe jump-to-APP.

The code is split into a chip-independent and a chip-dependent layer: `core/` holds the
state machines, protocol, storage and boot policy, while `port/<family>/<chip>/` holds
clock, flash, UART, GPIO, IWDG, SysTick and the atomic jump sequence; the two communicate
only through the ops abstraction in `port/bl_port.h`. `chips/<id>.json` is the build-side
source of truth (it generates the spec / scatter file / Keil project), so adding a chip
means creating a `port/` directory, filling in `chips/<id>.json` and implementing the ops
(see [docs/porting_guide.md](docs/porting_guide.md)).

**The unified entry point for board configuration is `board_config.h`**: pins, oscillator
and core clock, partitions and erase units, IWDG timeouts and optional-service switches
all live there. `port/<family>/<chip>/board_config.h` is the single C-side source of those
constants and must stay consistent with the build-side `chips/<id>.json` — after changing
either side, run `python chips/test_chip.py` to enforce it (item-by-item table under
"Configuration" below).

## Supported Chips

| Chip | Support scope | Validation status |
|---|---|---|
| STM32F103C8T6 (reference package) | Serial upgrade + boot/jump as the backbone; the optional Bluetooth (UART2) and OLED (soft I2C) capabilities are implemented as well | The only package with the complete hardware validation chain: 14/14 acceptance items, host-tool upgrade E2E, power-loss recovery drills |
| STM32F411CEU6 (minimal package) | UART upgrade + boot/jump only (no Bluetooth / OLED / I2C) | Build, consistency and on-target HIL pass: upgrade, jump, setmeta round-trip, reset injection |
| STM32G0 / H7 | Reserved port directories (skeletons) | Not implemented |
| STM32F407ZGT6 | Planned: second reuse point in the f4 family | Not started |

Both packages ship a **minimal default BL example configuration** (LED status + serial log,
ADR-019): Bluetooth and OLED are optional capabilities and are off by default (see
"Configuration" below). The F103 14/14 acceptance run, artifact sizes and SHA-256 values
were obtained with the example configuration that included OLED and Bluetooth; per-item
results are in [CHANGELOG.md](CHANGELOG.md).

## Quick Start

Full steps live in the **user manual** [docs/user_manual.md](docs/user_manual.md) (wiring,
first flash, daily upgrades, indicators, troubleshooting); below is the command summary.

```bash
# 1) Toolchain check (Git Bash / Linux; CMD: scripts\check_toolchain.bat)
bash scripts/check_toolchain.sh

# 2) Build BL + APP (chip.json + templates -> spec/sct -> project -> UV4 full rebuild -> bin)
CHIP=f103c8t6 bash scripts/build_keil.sh

# 3) Upgrade the APP and jump, using the host tool (sibling repo ../LiteBootUpgrader)
uv run --python 3.12 --with pyserial ../LiteBootUpgrader/bl_upgrade.py upgrade your_app.bin --port COMx
uv run --python 3.12 --with pyserial ../LiteBootUpgrader/bl_upgrade.py jump --port COMx
```

The BL itself is flashed once with a debug probe (the user manual §3 lists the pyocd
commands); after that every APP upgrade goes over serial. For the GUI, double-click
`../LiteBootUpgrader/bl_upgrade_gui.bat`.

> The compiler is pinned to **AC5** (V5.06u7, ADR-013) and the CMSIS core headers to V1.30
> (see `third_party/CMSIS/LICENSES.md`); for a non-standard Keil installation point the
> `KEIL_UV4` environment variable at UV4.exe. Builds are judged by exit code: 0 = no
> warnings or errors, 1 = warnings, >=2 = errors. Run Python tools in an isolated
> environment (`uv run --with <pkg>` or a venv) rather than polluting the global interpreter.

## Configuration: board_config.h

The single entry point for board configuration is `port/<family>/<chip>/board_config.h` (the
single C-side source of those constants); it must stay consistent with the build-side
`chips/<id>.json`. After changing either side:

```bash
python chips/test_chip.py              # enforce chip.json ↔ board_config.h + artifact template round-trip
CHIP=<id> bash scripts/build_keil.sh   # regenerate the artifacts and do a full rebuild
```

**Required** (determines whether the upgrade chain works — check against your hardware)

| Item | Where |
|---|---|
| Serial channel pins (the only mandatory service) | `BL_UART_TX_PORT`/`_NUM`, `BL_UART_RX_*` (USART1 PA9/PA10 by default) |
| Oscillator and core clock | `BL_HSE_MHZ` (the F411 accepts an 8 or 25 MHz crystal); the PLL parameters are selected from that macro in `port/<family>/<chip>/clock.c` |
| Partitions and erase units (BL / APP / parameter area) | `BL_FLASH_*` and `BL_ERASE_UNIT_TABLE`; mirror them into `chips/<id>.json` `partitions`/`erase_units` |
| IWDG timeout and the relaxed upgrade value | The IWDG macros (2000 / 8000 ms on F103, etc.) |

**Optional capabilities** (enable as needed; otherwise the weak defaults in
`core/bl_service_stub.c` apply)

| Capability | How to enable |
|---|---|
| LED status display | Link `services/display_led` (already part of the minimal configuration); pins `BL_PIN_LED*` |
| Serial log | Link `services/debug_uart` (already part of the minimal configuration); level/off via `BL_LOG_LEVEL` / `BL_LOG_DISABLE` |
| Bluetooth HC-05 channel (transport channel 1) | `BL_TRANSPORT_BT_EN=1` + the Bluetooth pins `BL_PIN_BT_STATE`/`_EN` (with `*_PORT`/`_NUM`) + add `uart2.c` back to `chips/<id>.json` `build.port_files_bl`; steps in [porting_guide §3.1](docs/porting_guide.md) |
| OLED display (SSD1306, soft I2C) | Link `services/display_oled` + `bsp/oled_ssd1306`; pins `BL_PIN_I2C_SCL`/`_SDA` (with `*_PORT`/`_NUM`) |

The pins of optional capabilities must be registered in `chips/<id>.json` `pins` as well
(`test_chip.py` checks both sides for consistency).

## Documentation

| Document | Contents |
|---|---|
| [docs/user_manual.md](docs/user_manual.md) | **User manual**: wiring, first flash, daily upgrades, indicators, troubleshooting |
| [docs/protocol.md](docs/protocol.md) | Protocol specification (frame format, commands, example frames, tool usage) — single protocol contract |
| [docs/partition.md](docs/partition.md) | Flash partitions, dual-copy parameter area state machine, power-loss recovery |
| [docs/external_interface.md](docs/external_interface.md) | External interface list (pins, protocol summary, abstract interfaces, OTA extension points) |
| [docs/architecture.md](docs/architecture.md) | Layered architecture, ops interfaces, runtime model, state machines, memory budget |
| [docs/porting_guide.md](docs/porting_guide.md) | Porting guide: adding a chip support package, ops requirements, config switches, pitfalls |

Development-facing docs (design ADRs, versioning rules, test plan, Bluetooth notes,
VOFA+ notes) live in [docs/dev/](docs/dev/) and [CONTRIBUTING.md](CONTRIBUTING.md).

## Contributing

- Bugs and chip-support requests: GitHub Issues (`bug_report` and `chip_support` templates)
- Contribution workflow (development environment, repository layout, three-repo coupling, CSP PR checklist, quality baseline): [CONTRIBUTING.md](CONTRIBUTING.md)
- Working rules for coding agents: [AGENTS.md](AGENTS.md)

## License

Original code of this project (`core/`, `port/`, `services/`, `bsp/`, `app/`, `linker/`,
`tools/`, `scripts/`, `docs/`) is released under the [MIT License](LICENSE) (© 2026 Qingc).

Third-party files bundled under `third_party/` are **verbatim copies** (unmodified) and
retain their original licenses and copyright notices; sources, license terms and
byte-level verification records are documented in
[third_party/CMSIS/LICENSES.md](third_party/CMSIS/LICENSES.md).
