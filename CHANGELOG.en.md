# Changelog

All notable changes to this project are documented in this file. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to
[Semantic Versioning 2.0.0](https://semver.org/).

中文版本：[CHANGELOG.md](CHANGELOG.md)

## [Unreleased]

### Fixed

- **F411 clock facts corrected**: the core board carries a 25 MHz HSE plus a
  **32.768 kHz RTC (LSE) crystal** — **not an 8 MHz crystal**. `chips/f411ceu6.json`
  drops the misleading `clock.hse_mhz_alt: 8` (now records `rtc_crystal_khz: 32.768`
  with a `_note`), and `board_config.h` (`BL_HSE_MHZ` comment), `clock.h`/`clock.c`
  comments, the README "crystal & clock" row, `docs/external_interface.md`,
  `docs/user_manual.md` and `docs/dev/design.md` follow. The 8 MHz PLL branch in
  `clock.c` (M=4/N=100) is **kept** but explicitly labelled as an option for
  self-modified boards with an 8 M crystal, not board-verified in this repo.
- **PC14/PC15 pin facts**: on the core board these two pins carry the RTC crystal, so
  they are not free GPIOs — reflected in `chips/f411ceu6.json`'s `bt_state_note`, the
  `BL_PIN_BT_STATE_PORT` comment in `board_config.h`, and the matching row in
  `docs/dev/test_plan.md` (`bt_state` stays an NC semantic placeholder; its value is
  unchanged).

### Notes

- No core/protocol or firmware-behavior change: no version bump (tracked under
  `[Unreleased]`).
- Dropping `hse_mhz_alt` from the chip manifest does not affect the build chain:
  chipfill/configgen only read `hse_mhz`/`target_hz`/`hsi_mhz`/`wait_states`, and the
  generated spec/sct carry no clock values (`chips/test_chip.py`'s manifest ↔
  board_config consistency and artifact round-trip checks stay green).
- Only the 25 MHz path has ever been board-tested on F411 (100MHz/3WS, see
  `docs/dev/test_plan.md` §5); the 8 MHz branch has no matching hardware and is only
  guaranteed to be selectable at compile time.

## [0.5.0] - 2026-09-30

Chip identity: the host tool can now identify the chip model unambiguously (ADR-021).
Protocol `VER` stays 0x01 — these are **append-only** fields: no frame-format change, no
new command.

### Added

- **Chip identity `BL_CHIP_DEVID`** (`board_config.h`, taken from the DBGMCU
  `IDCODE.DEV_ID[11:0]`: F103 medium density 0x410 / F411 0x431): kept in sync with
  `chips/<id>.json`'s `device.dev_id` by `chips/test_chip.py` (uniqueness is judged on
  `(dev_id, flash size)` — the F4 value 0x413 covers F405/407/415/417 of every density).
- **GET_INFO reports the chip identity** (protocol.md §5.2): the response grows 67 →
  **69 B** with a trailing LE16. The value does not depend on the parameter area, so even a
  factory-fresh device reports its model; §5.2.1 adds the identification order
  (`dev_id` + flash size → capacity fingerprint → read-only VERIFY probe → report the
  ambiguity).
- **The parameter area records the chip identity** (partition.md §4): copy bytes
  `0x25-0x26` hold the `BL_CHIP_DEVID` of the BL that wrote the record (LE16); the record
  image grows `0x25` → `0x28`. GET_META reports it (21 → **23 B**, protocol.md §5.7,
  `0xFFFF` = not recorded) for traceability — readable offline straight from the area.

### Changed

- `bl_meta_matches_app` now includes the chip identity in its equivalence test → a
  **self-healing path**: a pre-0.5.0 record gets the identity stamped along with the next
  VERIFY persistence, with no extra write cycle.
- Version: `core/bl_version.h` 0.4.0 → **0.5.0**; `docs/dev/versioning.md` synchronised
  (including its long-stale "current version" line).

### Notes

- **Compatibility**: old firmware reads only the first `0x25` bytes of a new record and its
  validity test ignores `0x25-0x28`, so its behaviour is unchanged; new firmware reading an
  old record sees `0xFF` there and treats it as "not recorded"; old hosts keep reading the
  67/21 B responses, new hosts parse by actual LEN (a short response simply carries no
  identity).
- **Sizes and artifacts (measured, AC5 -Ospace, 0 Error 0 Warning)**: F103 BL 12 588 →
  **12 756 B** (+168, limit 16 KiB, `ac82e600…`), F103 APP 8 364 → 8 396 B (+32,
  `23411221…`); F411 BL 12 996 → **13 168 B** (+172, limit 32 KiB, `706c44a9…`), F411 APP
  6 480 → 6 512 B (+32, `896ee6e3…`).
- Validation status: `chips/test_chip.py` **13/13** (new dev_id consistency case); full
  rebuilds of both chips with 0 errors 0 warnings; **verified on an actual STM32F103C8T6** —
  the selftest passes **15/15** (its `META persistence seq=1060` step goes through the new
  commit path), GET_INFO comes back **69 B** with `0x0410` in its tail and GET_META **23 B**
  with the same value; the **compatibility matrix and self-healing were exercised in
  sequence**: flash the 0.4.0 BL and write a legacy record (GET_META 21 B, no identity) →
  flash 0.5.0, read that record as `0xFFFF` (not recorded — no false identity) → one VERIFY
  persistence stamps it as `0x0410`. **No F411 board run** (no board available); its
  coverage is host-side tests plus the build.

## [0.4.0] - 2026-09-30

Optional signature verification for F411 (ADR-020, landing the ADR-017 hook point):
ECDSA P-256 + SHA-256. Additive protocol command `0x11 VERIFY_SIGNED`; VER 0x01
unchanged.

### Added

- **Signature verification as an option (F411)**: `BL_SIGN_EN` build switch
  (default 0; mainline behavior/size equivalent to 0.3.1 — signature sources are
  stripped as unreferenced, measured +252 B on both chips for the auth plumbing
  alone); enabled variant measures F411 BL 18 784 B ≤ 32 KiB (+5 788 B for the
  crypto core), and the build **must emit an enable warning** (AC5 has no
  `#pragma message`/`#warning` — implemented via an unreferenced static array
  raising #177-D so the notice text lands in the build log).
- **0x11 VERIFY_SIGNED** (protocol.md §5.11): DATA = size + crc32 + signature
  (64B, big-endian r‖s); CRC + SHA-256 computed over a single read pass (watchdog
  fed per chunk) → uECC verify → persist auth=1 only on success; failures persist
  nothing. New status `SIGN_ERROR 0x06`. F103 does not support it (16K budget,
  signature sources not in its build; 0x11 answers STATE_ERROR as unknown).
- **Authentication flag**: parameter-region reserved byte 0x24 = auth
  (partition.md §4, backward compatible both ways); `bl_boot_app_valid` requires
  auth=1 when BL_SIGN_EN=1 — zero crypto at boot; images persisted via legacy
  VERIFY cannot jump. `bl_storage_verify_app` split into check (no persist) +
  persist stages.
- **micro-ecc third-party library** (`third_party/micro-ecc/`, BSD-2-Clause,
  upstream commit `541b3a7`): pure-C path (via `uECC_PLATFORM=0` compile define,
  no asm bundle), secp256r1 only; source/hash/whitespace-normalization notes in
  its LICENSES.md.
- **`tools/sign_image.py`**: test keypair generation (`--keygen`) and 0x11 frame
  assembly (`--sign`), run under uv isolation (`--with cryptography`); for
  HIL/development — upgrade-time signing integration comes to LBU later
  (together with a keypair-generator module).
- **Docs**: ADR-020; protocol.md §5.11 + §9.1 v1.4.0; partition.md §4 auth-byte
  backfill; architecture.md §10 verification chain; external_interface.md
  command/status/capability rows; porting_guide §3.1 signing checklist;
  user_manual signing section.

### Security

- **No key material ever enters git** (user requirement): the public key is a
  deployment-local header `bl_sign_pubkey_local.h` (gitignored; missing file
  fails the build with `#error`), the repo carries only the format template
  `docs/dev/bl_sign_pubkey_local.template.h`; test public keys are likewise kept
  out of git, keypairs generated locally. Threat model: a keyless host cannot
  make the BL accept an image; physical/debug-port attacks and rollback are out
  of scope.

## [0.3.1] - 2026-09-30

Fixes from the 2026-09-29 audit (`docs/review/audit-2026-09-29.md`, local document):
one P1 conditional guard bypass + four P2 robustness items + P3 consistency gaps.
No protocol frame or command changes (VER 0x01 unchanged).

### Fixed

- **FAULT-state command whitelist (P1-1)**: after a geometry self-check failure
  (FAULT) only PING/GET_INFO/RESET diagnostics are served, everything else answers
  `STATE_ERROR` — previously FAULT still answered ERASE_APP, and the covering-unit
  table filled by `bl_storage_init` before failing would let the erase reach the
  BL/parameter region (requires a CSP with wrong geometry; released F103/F411
  configs are unaffected); `bl_meta_load` sets its checked flag only after the
  check passes (was set before, letting later calls skip it); `bl_storage_*`
  erase/write entry points gained a second `s_geom_ok` defence line
- **WRITE_CHUNK auto-erase widens IWDG (P2-1)**: `handle_write` now calls
  `wdg_widen_for_upgrade()` symmetrically with `handle_erase` — third-party hosts
  that skip ERASE_APP no longer risk a 128K sector erase (~1.75 s) hitting the
  2 s normal watchdog window on F4; protocol.md §5.3 updated
- **APP example frame buffer +3B (P2-2)**: `APP_FRAME_MAX` now computed from the
  11-byte frame overhead (`11u + BL_FRAME_DATA_MAX`) — the old 8+256=264 made
  frames with LEN ≥ 254 (incl. the 256B protocol maximum) impossible to frame,
  silently dropped; fixed in both chip APP examples
- **IWDG runtime reconfig waits RVU before writing (P3-3)**: both chip `wdg.c`
  `set_timeout_ms` wait for RVU clear before writing RLR (RM0008 §20.4.5 /
  RM0390), removing the "widened but actually ignored" race
- **OLED chip facts centralized (P2-4)**: `board_config.h` gains `BL_CHIP_NAME`
  (F103C8/F411CE); the display_oled service consumes the macro plus the runtime
  measured clock (HSI fallback shown as `*`), no chip facts left in the service
- **i2c.c consumes board macros (audit addendum)**: PB8/PB9 hardcoding replaced
  by `BL_PIN_I2C_*_PORT/NUM` derivation (port clock enable / CRL / CRH selected
  at compile time), making "change the central config to change the pins" hold
  for the I2C pair too

### Changed

- `chips/f103c8t6.json` `device.cpu_clock` corrected `CLOCK(12000000)` →
  `CLOCK(8000000)` (on-board 8 MHz crystal; Keil simulation display only),
  spec/sct/uvprojx regenerated
- `scripts/build_keil.sh` compares the produced bin against the
  `chips/<id>.json` partition limit and fails the build on overflow (AGENTS §9.4
  baseline automation)
- Doc/implementation alignment: protocol.md §5.5 documents the VERIFY 4-byte
  alignment rejection, §6 retry wording corrected to "3 attempts total" with the
  actual 8.0 s single-shot / 5.0 s retry-layer erase timeouts; partition.md §10
  adds `bl_meta_matches_app` and switches to the "delivered" wording;
  tools/vofa+/README.md cross-repo path fixed

## [0.3.0] - 2026-09-29

Second chip support package: STM32F411CEU6 minimal implementation (ADR-017, CSP
phase B); optional-service model and minimized BL example config (ADR-019).

### Added

- **F411CEU6 support package** (`chips/f411ceu6.json` + `port/stm32f4/f411ceu6/`): UART
  upgrade + boot/jump only — no OLED/Bluetooth/I2C. Partitions: BL 32K (sectors 0-1),
  params 2×16K (sectors 2/3, independent erase units), APP 448K (sectors 4-7); clock
  `BL_HSE_MHZ` supports both 8/25 MHz crystals (100MHz/3WS, PWR VOS Scale 1 first, HSI
  fallback); IWDG 2000ms relaxed to 8000ms during upgrade (CPU stalls cannot feed during
  128K sector erase; PR=/256 covers both levels); `BL_TRANSPORT_BT_EN=0` unregisters the
  BT channel (slot reserved)
- **LED-only display service** (`services/display_led/`): minimal display for OLED-less
  packages, mode table per ADR-008
- **Multi-chip build finishing**: `chips/test_chip.py` auto-discovers all chip manifests
  and validates non-uniform erase units (F4 explicit sector table, both sides); spec
  template Port/Services/BSP lists and include/scatter paths now chip.json-driven;
  artifacts split per chip (`chips/<id>/`, `linker/<id>/`; f103c8t6 keeps its legacy
  root slots with byte-identical renders)
- **F4 CMSIS headers** (`third_party/CMSIS/`): Core(M) V5.6.0 + Device STM32F4xx (AC5
  compatible, Apache-2.0), byte-identical copies from local STM32Cube_FW_F4_V1.28.3 with
  SHA-256 records in LICENSES.md; new `scripts/vendor_copy.py` copy-and-verify tool
- **F411 minimal APP example** (`app/examples/f411ceu6_app/`): breathing LED + upgrade
  request responder
- **`scripts/pyocd_manual_flash.py`**: register-level flashing tool for the F411 board
  (workaround for the broken pyocd flash-algo path on this board; verifies after a reset
  to flush the F4 ART cache)

### Changed

- **Optional-service model (ADR-019)**: the only mandatory service is the wired UART
  channel. `core/bl_service_stub.c` provides `__weak` defaults for
  `bl_display`/`bl_debug`/`bl_port_i2c_release` — display/debug become link-time
  optional plugins; service hardware bootstrap moved into each service's init
  (main.c assembles only the mandatory chain). Verified on both boards: F103 selftest
  15/15, display-less variant completes the full flow over serial, F411 upgrade/jump
  re-verified
- **BL example config minimized**: the F103 default BL config drops the original
  full-featured list (OLED/Bluetooth/I2C out of the build, `BL_TRANSPORT_BT_EN=0`),
  unified to "minimal usable" = `display_led` (LED status) + `debug_uart` (serial
  logs), same shape as the F411 minimal package; **APP example configs unchanged**
  (F103 APP keeps the OLED demo; BSP lists split into `bsp_files_bl`/`bsp_files_app`).
  OLED/Bluetooth remain optional capabilities — see porting_guide §3.1
- **Pins promoted to board-level declarations (ADR-018)**: `board_config.h` is now the
  single source of pin facts (logical id + `*_PORT` port index + `*_NUM` pin number +
  `BL_PIN_LED_ACTIVE_LOW` polarity); gpio.c only consumes them (map/ops paths are
  macro-driven, F1 CRL/CRH config derived from NUM); `chips/test_chip.py` enforces
  chip.json `pins` ↔ declarations (led/bt_state/bt_en/i2c_*; uart_* are port-record
  only). Bins grew slightly from macro-driven init (see Verification)

### Verification

- `chips/test_chip.py` 12/12 passed (f103c8t6 + f411ceu6, including the pins and erase-unit
  sections)
- F103 (AC5 full rebuild, 0 errors 0 warnings; re-run 2026-09-29 reproduces these bytes):
  default minimal BL **12 180 B** ≤ 16K (SHA-256 `ac658278…`), APP **8 276 B**
  (`d1286297…`); display-less over-serial variant 12 616 B; full variant with OLED +
  Bluetooth 15 464 B (`ebb2f93f…`, the +64 B cost of the weak stubs). The pin-abstraction
  step on its own gave 15 400 B / 8 228 B (`31dc570f…` / `3d5301c4…`), superseded by the
  minimization above
- F411 (AC5 full rebuild, 0 errors 0 warnings; re-run 2026-09-29 reproduces these bytes):
  BL **12 584 B** ≤ 32K (SHA-256 `4277840d…`), APP **6 384 B** (SHA-256 `fe11b440…`)
- F103 baseline check: byte-identical to the 0.2.0 release before the pin abstraction
  (BL 15 324 B `499de4bc…` / APP 8 152 B `534eb656…`); afterwards behavior-equivalent,
  sizes as above
- **F411 on-target HIL completed (2026-09-29)**: flash (register-level, see
  `scripts/pyocd_manual_flash.py`) → GET_INFO (flash=512KB/UID correct) → upgrade
  (448K erase 4.21s with no reset under the 8s IWDG relaxation, VERIFY CRC32 match) →
  jump to breathing-LED APP → setmeta round-trip back to BL (monotonic seq) → reset
  injected mid-erase recovered → auto-jump on valid APP at boot; selftest 15 steps:
  14 PASS + 1 false-FAIL (LBU fixture hardcodes F103 `APP_SIZE=0xB800`, see
  docs/dev/test_plan.md §5.1)
- Signature/hash verification confirmed as a separate iteration (integration point
  documented in docs/partition.md §4)

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
