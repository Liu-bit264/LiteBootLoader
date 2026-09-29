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

| Chip | Status | Notes |
|---|---|---|
| STM32F103C8T6 | Full-featured | The only support package with the complete hardware validation chain: 14/14 acceptance items, host-tool upgrade E2E, power-loss recovery drills |
| STM32F411CEU6 | Minimal package | UART upgrade + boot/jump only (no OLED / Bluetooth / I2C); upgrade, jump, setmeta round-trip and reset injection verified on target |
| STM32G0 / H7 | Reserved | Skeletons only; complete them following the porting guide |
| STM32F407ZGT6 | Planned | Second reuse point in the f4 family |

Artifact sizes, SHA-256 values and the per-item acceptance results are recorded in
[CHANGELOG.md](CHANGELOG.md).

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

| What to change | Where |
|---|---|
| Pins: USART TX/RX, LED, Bluetooth STATE/EN, soft-I2C SCL/SDA | `board_config.h`: `BL_UART_TX_PORT`/`_NUM`, `BL_UART_RX_*`, `BL_PIN_LED*`, `BL_PIN_BT_*`, `BL_PIN_I2C_*`; mirror them into `chips/<id>.json` `pins` |
| Oscillator and core clock (e.g. an 8 MHz vs 25 MHz crystal) | `board_config.h` `BL_HSE_MHZ`; the PLL parameters are selected from that macro in `port/<family>/<chip>/clock.c` |
| Partitions and erase units (BL / APP / parameter area) | `board_config.h` `BL_FLASH_*` and `BL_ERASE_UNIT_TABLE`; mirror them into `chips/<id>.json` `partitions`/`erase_units` |
| IWDG timeout and the relaxed upgrade value | The IWDG macros in `board_config.h` (2000 / 8000 ms on F103, etc.) |
| Optional services: Bluetooth channel, OLED, log level | The service switches in `board_config.h` (e.g. `BL_TRANSPORT_BT_EN`, `BL_LOG_*`); services that are not enabled fall back to the weak defaults in `core/bl_service_stub.c` — enabling steps are in [porting_guide §3.1](docs/porting_guide.md) |

After changing either side: `python chips/test_chip.py` (enforces chip.json ↔
board_config.h consistency plus artifact template round-trip), then
`CHIP=<id> bash scripts/build_keil.sh` (regenerates the artifacts and does a full rebuild).

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
