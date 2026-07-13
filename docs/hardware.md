# Hardware reference

This is a reference for the board overlays and antenna/path presets used by
this firmware. To build from source, see
[docs/build-from-source.md](build-from-source.md); to flash prebuilt firmware,
see [docs/flash-quickstart.md](flash-quickstart.md).

This document is a reference table only — it does not cover physical antenna
wiring, RF placement, antenna tuning, the COBS wire format, or the
mars-bluetooth-hci API.

## Supported boards

Five of the target boards build for `BOARD=nrf54l15dk/nrf54l15/cpuapp` with the
carrier selected by overlay. The nRF54L15 TAG is the exception: it builds
against its own base board `nrf54l15tag/nrf54l15/cpuapp`, because the tag
overlay's `/delete-node/ &sky13348` and `antenna_switch_v1`/`antenna_switch_v2`
target nodes that exist only in the nrf54l15tag DTS (see issue #40).

| Board | Overlay | COBS UART | Console UART | Antenna-switch GPIOs |
|-------|---------|-----------|--------------|----------------------|
| nRF54L15DK | `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | `uart20` @ 921600 | `uart30` @ 921600 | `P1.09`, `P1.10` |
| nRF54L15 TAG | `boards/nrf54l15tag_nrf54l15_cpuapp.overlay` | `uart20` @ 921600 | — (RTT via debug probe) | `P1.09`, `P1.10` |
| U-Blox NINA-B40 | `boards/ublox_nrf54l15_cpuapp.overlay` | `uart30` @ 921600 | `uart20` @ 921600 | `P1.09`, `P1.08` |
| Ezurio BL54L15u | `boards/ezurio_bl54l15u_nrf54l15_cpuapp.overlay` | `uart20` @ 921600 | `uart30` @ 921600 | `P1.09`, `P1.08` |
| Fanstel BM15C | `boards/fanstel_bm15c_nrf54l15_cpuapp.overlay` | `uart20` @ 921600 | `uart30` @ 921600 | — (no antenna-switch node) |
| Minewsemi ME54BE01 | `boards/minew_me54be01_nrf54l15_cpuapp.overlay` | `uart20` @ 921600 | — (RTT via debug probe) | — (no antenna-switch node) |

GPIOs use Zephyr devicetree port-pin notation (`&gpio1 9` → `P1.09`, port 1
pin 9). All antenna-switch `ant-gpios` are `GPIO_ACTIVE_HIGH`. The DK, U-Blox,
and Ezurio antenna nodes set `multiplexing-mode = <1>`; the TAG sets `<0>` (its
`sky13348` RF switch and the board's default `antenna_switch_v1`/`v2` gpio-hogs
are deleted by the overlay to avoid duplicate access to the `P1.09`/`P1.10`
antenna pins). Board names follow the `displayName` strings in
`CMakePresets.json` — except U-Blox, whose `displayName` is just `U-Blox`
(shown here as `U-Blox NINA-B40`), and the TAG, whose `displayName` `nRF54L15TAG`
is shown here as `nRF54L15 TAG`.

### UART assignments

- `cobs-uart` is the authoritative chosen node for the COBS ranging stream,
  consumed by `initiator/src/serialize.c` via `DEVICE_DT_GET(DT_CHOSEN(cobs_uart))`.
  On boards with a console UART (all except the TAG and the ME54BE01), the console UART also
  carries shell, mcumgr, bt-mon, and bt-c2h — all five `zephyr,console` /
  `shell-uart` / `uart-mcumgr` / `bt-mon-uart` / `bt-c2h-uart` chosen nodes point
  to it. The TAG has no console UART (see [TAG wiring notes](#nrf54l15-tag-wiring-notes)).
- **U-Blox swaps** COBS and console versus the DK: COBS on `uart30`, console on
  `uart20`. Ezurio matches the DK assignment. Don't assume a fixed mapping.
- **Physical TX/RX pins are not in the overlays** except Fanstel's and the TAG's
  COBS `uart20` (`P1.13` TX / `P1.14` RX), which both `#include` the shared
  `boards/uart20_p1_13_p1_14.dtsi` (the nrf54l15tag base DTS ships no UART
  pinctrl at all). For the DK's pins see
  [docs/flash-quickstart.md](flash-quickstart.md#read-the-ranging-output)
  (console `uart30` = `P0.00`/`P0.01`, COBS `uart20` = `P1.04`/`P1.05`, sourced
  from the NCS base DTS). U-Blox and Ezurio physical pins are not in this repo —
  see the carrier-board documentation.

### nRF54L15 TAG wiring notes

The TAG is not a DK and needs extra hardware to expose the COBS stream:

- **No onboard USB.** Unlike the nRF54L15 DK, the TAG has no USB interface and no
  onboard J-Link/UART bridge, so its UARTs are not exposed as virtual COM ports
  over a debug USB cable. To read the COBS ranging stream you **must wire an
  external UART-to-USB adapter** (e.g. an FT232R breakout) to the COBS UART:
  `uart20` **TX → P1.13**, **RX → P1.14** (`921600` baud, 8N1, no flow control).
  This is the pin assignment encoded in the tag overlay's `uart20` pinctrl.
- **Single UART; console over RTT.** The TAG exposes only one UART — `uart20`,
  used for the COBS ranging stream. There is no console/shell UART: the tag
  overlay binds only `cobs-uart = &uart20` and `boards/nrf54l15tag.conf` selects
  the RTT console backend (`CONFIG_USE_SEGGER_RTT=y` / `CONFIG_CONSOLE=y` /
  `CONFIG_RTT_CONSOLE=y`), so the console (log output) runs over Segger RTT via
  the debug probe (J-Link through the DK `DEBUG OUT` header) with
  no extra wiring. This matches Nordic's nrf54l15tag board guidance (RTT/NUS for
  console).
- **Power and programming.** The TAG is powered and programmed through an
  nRF54L15 DK's `DEBUG OUT` header (the DK's onboard debugger is rerouted to the
  TAG's SoC) or a CR2032 coin cell for power — see the
  [nRF54L15 TAG board documentation](https://docs.nordicsemi.com/bundle/ncs-latest/page/zephyr/boards/nordic/nrf54l15tag/doc/index.html).
- **UART driver Kconfig.** The nrf54l15tag board defconfig enables only GPIO and
  MPU (its default console path is RTT), so it does not set `CONFIG_SERIAL`.
  That is an app-level requirement — the initiator's COBS transport
  (`src/serialize.c`, `DEVICE_DT_GET(DT_CHOSEN(cobs_uart))`) needs a UART driver
  device — so `CONFIG_SERIAL=y` lives in `initiator/prj.conf` (the
  nrf54l15dk board defconfig also sets it for the DK presets). Without it the
  `uart20` device the initiator's `cobs-uart` acquires is never instantiated and
  the link fails with an undefined `__device_dts_ord_<N>`.

### Minewsemi ME54BE01 wiring notes

The ME54BE01 is a third-party module dev board (ME54BS01-nRF54L15), not a DK;
like the TAG it exposes a single UART and uses RTT for console, but it differs
in two ways: it has an onboard USB-to-serial bridge (Type-C) for the COBS UART,
and no onboard debugger. It builds on `BOARD=nrf54l15dk/nrf54l15/cpuapp` (there
is no NCS board def for the ME54BE01) — the overlay selects the carrier.

- **No onboard debugger.** The ME54BE01 has no onboard J-Link; flashing and RTT
  both need an external J-Link (or an nRF54L15 DK `DEBUG OUT` header) wired to the
  module's SWDIO (`P0.02`) / SWDCLK (`P0.03`) pins.
- **Single UART; console over RTT.** Only `uart20` is exposed — the COBS ranging
  stream via `cobs-uart`, over the onboard USB-to-serial bridge (Type-C). There
  is no console/shell UART: the overlay binds only `cobs-uart = &uart20` and
  `boards/minew_me54be01.conf` selects the RTT console backend (and disables the
  DK defconfig's `CONFIG_UART_CONSOLE`, since the overlay defines no
  `zephyr,console` node), so the console (log output) runs over Segger RTT via the
  same external debug probe, with no extra wiring.
- **Power and COBS.** Type-C provides both board power and the COBS UART bridge
  (`uart20` TX `P1.05` / RX `P1.04`, `921600` baud, 8N1) — no external
  UART-to-USB adapter is needed (unlike the TAG). The COBS `uart20` pins are
  inherited from the nrf54l15dk base DTS (the ME54BE01 builds on
  `nrf54l15dk/nrf54l15/cpuapp`), the same `P1.04`/`P1.05` as the DK.
- **UART driver Kconfig.** The ME54BE01 builds on the nrf54l15dk board defconfig,
  which sets `CONFIG_SERIAL=y` (so the `uart20` `cobs-uart` device instantiates
  with no app-level `CONFIG_SERIAL` line — unlike the TAG, which needs it in
  `initiator/prj.conf`) and `CONFIG_UART_CONSOLE=y` (disabled here in favour of
  the RTT console). See the
  [ME54BE01 dev board datasheet](https://store.minewsemi.com/wp-content/uploads/2024/11/Development_Board_ME54BE01_Datasheet_EN.pdf)
  and the [ME54BS01 module datasheet](https://store.minewsemi.com/wp-content/uploads/2025/02/ME54BS01-nRF54L15_Datasheet_K_EN.pdf).

### Antenna-switch node

- The `cs_antenna_switch` node (`compatible = "nordic,bt-cs-antenna-switch"`,
  `multiplexing-mode = <1>` — the TAG sets `<0>`) is owned and consumed internally by the Nordic
  controller library in NCS; no code in this repo reads `ant-gpios` directly.
  The overlay comment "See `cs_antenna_switch.c`" refers to NCS-owned source, not
  a file in this repo.
- Fanstel and the Minewsemi ME54BE01 have no `cs_antenna_switch` node
  (single-antenna boards; the Nordic controller's `cs_antenna_switch.c` is
  compiled only under `CONFIG_BT_CTLR_SDC_CS_MULTIPLE.ANTENNA_SUPPORT`, i.e.
  `NUM_ANTENNAS >= 2`, and both boards' presets use `NUM_ANTENNAS=1`). The
  Fanstel overlay instead sets `&lfxo` load-capacitance to 15.5 pF (`15500` fF) —
  a factual board-clock difference recorded here as overlay content, not tuning
  guidance.

## Antenna and path presets

### What "A1 1-path" and "A2 4-path" mean

"A1" / "A2" is the antenna count, set by `CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS`.
"1-path" / "4-path" is the antenna-path count, set by
`CONFIG_BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS` (mirrored on the host side by
`CONFIG_BT_RAS_MAX_ANTENNA_PATHS`). So "A1 1-path" means 1 antenna, 1 path;
"A2 4-path" means 2 antennas, 4 paths.

### Kconfig fragments

Every preset pulls a `boards/*_local.conf` path-local fragment. RAS presets pull
just that one (plus `central.overlay` for initiators, and `nrf54l15tag.conf` for
the TAG presets' RTT-console/serial-driver Kconfig — see
[TAG wiring notes](#nrf54l15-tag-wiring-notes)). Each local fragment is three
lines setting the same three symbols:

| Fragment | `RAS_MAX_ANTENNA_PATHS` | `SDC_CS_MAX_ANTENNA_PATHS` | `SDC_CS_NUM_ANTENNAS` | Antennas / Paths |
|----------|------------------------|----------------------------|------------------------|------------------|
| `1_path_1_local.conf` | 1 | 1 | 1 | A1 / 1-path |
| `2_path_1_local.conf` | 2 | 2 | 1 | A1 / 2-path |
| `2_path_2_local.conf` | 2 | 2 | 2 | A2 / 2-path |
| `4_path_1_local.conf` | 4 | 4 | 1 | A1 / 4-path |
| `4_path_2_local.conf` | 4 | 4 | 2 | A2 / 4-path |
| `4_path_4_local.conf` | 4 | 4 | 4 | A4 / 4-path |

Fragment filenames follow `<paths>_path_<antennas>_local.conf` — paths first,
antennas second.

IPT presets pull the same path-local fragment **plus** two Inline PCT fragments
that switch the firmware from RAS to IPT mode (see
[docs/architecture.md](architecture.md) for what IPT mode is). Applied via
`EXTRA_CONF_FILE` after `CONF_FILE`, they override the RAS defaults:

| Fragment | Applied to | Purpose |
|----------|-----------|---------|
| `boards/inline_pct_shared.conf` | both initiator & reflector | Sets `CONFIG_BT_DEVICE_NAME="Nordic CS IPT Reflector"` — the single source of truth for the IPT reflector's advertised name. The reflector advertises it; the initiator scans for it by name instead of the Ranging Service UUID. |
| `boards/inline_pct_initiator.conf` | initiator only | Enables `CONFIG_MARS_CS_INLINE_PCT=y`, disables RAS (`CONFIG_BT_RAS=n`…), drops the GATT client / large MTU (the reflector's PCT contribution is embedded in the local tones — no RAS ranging-data transfer), and sets `CONFIG_BT_SCAN_NAME_CNT=1` to discover the reflector by name. |
| `boards/inline_pct_reflector.conf` | reflector only | Enables `CONFIG_MARS_CS_INLINE_PCT=y` + `CONFIG_BT_CTLR_EXTENDED_FEAT_SET=y` and disables the RAS responder (`CONFIG_BT_RAS_RRSP=n`…). |

### Presets

Each preset is either **RAS** (Ranging Service) or **IPT** (Inline PCT); the
Mode column marks which. The two are peer choices — see
[docs/architecture.md](architecture.md) for the RAS-vs-IPT contrast and the IPT
data flow. RAS and IPT share the same board overlays and path-local fragments;
IPT presets additionally pull the `inline_pct_*.conf` fragments above. There is
no TAG IPT preset — IPT covers the five carrier boards below (A1/A2 on the
initiator, A1 4-path on the reflector, with the Ezurio reflector at A2 4-path).

| Preset | Mode | Role | Board | Overlay | `EXTRA_CONF_FILE` | Antennas / Paths |
|--------|------|------|-------|---------|-------------------|------------------|
| `nrf54l15dk_cent_a1_1` | RAS | initiator | nRF54L15DK | `nrf54l15dk_*.overlay` | `central.overlay;1_path_1_local.conf` | A1 / 1 |
| `nrf54l15dk_cent_a1_2` | RAS | initiator | nRF54L15DK | `nrf54l15dk_*.overlay` | `central.overlay;2_path_1_local.conf` | A1 / 2 |
| `nrf54l15dk_cent_a1_4` | RAS | initiator | nRF54L15DK | `nrf54l15dk_*.overlay` | `central.overlay;4_path_1_local.conf` | A1 / 4 |
| `nrf54l15dk_cent_a4_4` | RAS | initiator | nRF54L15DK | `nrf54l15dk_*.overlay` | `central.overlay;4_path_4_local.conf` | A4 / 4 |
| `nrf54l15dk_cent_a1_1_ipt` | IPT | initiator | nRF54L15DK | `nrf54l15dk_*.overlay` | `central.overlay;inline_pct_initiator.conf;inline_pct_shared.conf;1_path_1_local.conf` | A1 / 1 |
| `nrf54l15tag_cent_a2_4` | RAS | initiator | nRF54L15 TAG | `nrf54l15tag_*.overlay` | `central.overlay;4_path_2_local.conf;nrf54l15tag.conf` | A2 / 4 |
| `ublox_cent_a1_1` | RAS | initiator | U-Blox NINA-B40 | `ublox_*.overlay` | `central.overlay;1_path_1_local.conf` | A1 / 1 |
| `ublox_cent_a1_1_ipt` | IPT | initiator | U-Blox NINA-B40 | `ublox_*.overlay` | `central.overlay;inline_pct_initiator.conf;inline_pct_shared.conf;1_path_1_local.conf` | A1 / 1 |
| `ezurio_bl54l15u_cent_a2_4` | RAS | initiator | Ezurio BL54L15u | `ezurio_*.overlay` | `central.overlay;4_path_2_local.conf` | A2 / 4 |
| `ezurio_bl54l15u_cent_a2_4_ipt` | IPT | initiator | Ezurio BL54L15u | `ezurio_*.overlay` | `central.overlay;inline_pct_initiator.conf;inline_pct_shared.conf;4_path_2_local.conf` | A2 / 4 |
| `fanstel_bm15c_cent_a1_1` | RAS | initiator | Fanstel BM15C | `fanstel_*.overlay` | `central.overlay;1_path_1_local.conf` | A1 / 1 |
| `fanstel_bm15c_cent_a1_1_ipt` | IPT | initiator | Fanstel BM15C | `fanstel_*.overlay` | `central.overlay;inline_pct_initiator.conf;inline_pct_shared.conf;1_path_1_local.conf` | A1 / 1 |
| `minew_me54be01_cent_a1_1` | RAS | initiator | Minewsemi ME54BE01 | `minew_me54be01_*.overlay` | `central.overlay;1_path_1_local.conf;minew_me54be01.conf` | A1 / 1 |
| `minew_me54be01_cent_a1_1_ipt` | IPT | initiator | Minewsemi ME54BE01 | `minew_me54be01_*.overlay` | `central.overlay;inline_pct_initiator.conf;inline_pct_shared.conf;1_path_1_local.conf;minew_me54be01.conf` | A1 / 1 |
| `nrf54l15dk_peri_a1_4` | RAS | reflector | nRF54L15DK | `nrf54l15dk_*.overlay` | `4_path_1_local.conf` | A1 / 4 |
| `nrf54l15dk_peri_a2_2` | RAS | reflector | nRF54L15DK | `nrf54l15dk_*.overlay` | `2_path_2_local.conf` | A2 / 2 |
| `nrf54l15dk_peri_a4_4` | RAS | reflector | nRF54L15DK | `nrf54l15dk_*.overlay` | `4_path_4_local.conf` | A4 / 4 |
| `nrf54l15dk_peri_a1_4_ipt` | IPT | reflector | nRF54L15DK | `nrf54l15dk_*.overlay` | `inline_pct_reflector.conf;inline_pct_shared.conf;4_path_1_local.conf` | A1 / 4 |
| `nrf54l15tag_peri_a2_4` | RAS | reflector | nRF54L15 TAG | `nrf54l15tag_*.overlay` | `4_path_2_local.conf;nrf54l15tag.conf` | A2 / 4 |
| `ezurio_bl54l15u_peri_a2_4` | RAS | reflector | Ezurio BL54L15u | `ezurio_*.overlay` | `4_path_2_local.conf` | A2 / 4 |
| `ezurio_bl54l15u_peri_a2_4_ipt` | IPT | reflector | Ezurio BL54L15u | `ezurio_*.overlay` | `inline_pct_reflector.conf;inline_pct_shared.conf;4_path_2_local.conf` | A2 / 4 |
| `fanstel_bm15c_peri_a1_4` | RAS | reflector | Fanstel BM15C | `fanstel_*.overlay` | `4_path_1_local.conf` | A1 / 4 |
| `fanstel_bm15c_peri_a1_4_ipt` | IPT | reflector | Fanstel BM15C | `fanstel_*.overlay` | `inline_pct_reflector.conf;inline_pct_shared.conf;4_path_1_local.conf` | A1 / 4 |
| `ublox_peri_a1_4` | RAS | reflector | U-Blox NINA-B40 | `ublox_*.overlay` | `4_path_1_local.conf` | A1 / 4 |
| `ublox_peri_a1_4_ipt` | IPT | reflector | U-Blox NINA-B40 | `ublox_*.overlay` | `inline_pct_reflector.conf;inline_pct_shared.conf;4_path_1_local.conf` | A1 / 4 |
| `minew_me54be01_peri_a1_4` | RAS | reflector | Minewsemi ME54BE01 | `minew_me54be01_*.overlay` | `4_path_1_local.conf;minew_me54be01.conf` | A1 / 4 |
| `minew_me54be01_peri_a1_4_ipt` | IPT | reflector | Minewsemi ME54BE01 | `minew_me54be01_*.overlay` | `inline_pct_reflector.conf;inline_pct_shared.conf;4_path_1_local.conf;minew_me54be01.conf` | A1 / 4 |

- **Naming asymmetry.** Preset names are `<board>_<cent|peri>_a<antennas>_<paths>`
  — antennas first, paths second (e.g. `ezurio_bl54l15u_cent_a2_4` = A2, 4 paths).
  Fragment filenames are `<paths>_path_<antennas>_local.conf` — paths first,
  antennas second. The two orders are reversed; cross-check carefully.
- **Initiator vs reflector.** Initiator presets include `central.overlay` (the
  scanning-central Kconfig fragment, mislabeled `.overlay` but a conf fragment)
  in `EXTRA_CONF_FILE`; reflector presets do not. `central.overlay` is a Kconfig
  fragment, not a devicetree overlay.

### How a preset composes overlay + fragments

Each preset sets `DTC_OVERLAY_FILE` (the board overlay) and `EXTRA_CONF_FILE`
(the role fragment `;`-separated from the path-local fragment; the TAG presets
also append `../boards/nrf54l15tag.conf` for RTT-console/serial-driver Kconfig).
IPT presets insert the `inline_pct_initiator.conf` / `inline_pct_reflector.conf`
and `inline_pct_shared.conf` fragments between the role fragment and the
path-local fragment (see the preset table above).
`ci/common.sh`
parses `CMakePresets.json`, splits `EXTRA_CONF_FILE` on `;`, and resolves each
part relative to the app directory; `ci/build.sh` passes them to
`west build -b <BOARD>` as `-DDTC_OVERLAY_FILE`, `-DEXTRA_CONF_FILE`, and
`-DCONF_FILE`. `BOARD` is always `nrf54l15dk/nrf54l15/cpuapp`; the overlay
selects the carrier. See [docs/build-from-source.md](build-from-source.md) for the full build flow.

### Tone-antenna configuration (supplementary)

The firmware maps the Kconfig counts to a BLE CS tone-antenna configuration
(`An_Bm`) in `common/antenna.c`. With `paths_per_antenna =
CONFIG_BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS /
CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS` (integer division), `get_antenna_config()`
returns `ANTENNA_MAPPING[NUM_ANTENNAS - 1][paths_per_antenna - 1]`. For the
shipped presets this resolves to: A1 1-path → `A1_B1`; A1 4-path → `A1_B2`; A2
4-path → `A2_B2`.

_Supplementary — from `common/antenna.c`, not from presets/fragments._

## Out of scope

- Physical antenna wiring diagrams.
- RF placement guidance.
- Antenna tuning advice (the LFXO load-capacitance note above is factual
  overlay content, not tuning guidance).
- The COBS wire format and the mars-bluetooth-hci API (see the
  [mars-bluetooth-hci](https://github.com/Metirionic/mars-bluetooth-hci) repo).
- A generated documentation site.

## References

- [README](../README.md) — project overview, firmware variants, and the flash and build entry points.
- [docs/flash-quickstart.md](flash-quickstart.md) — flashing prebuilt firmware;
  nRF54L15DK physical UART pins.
- [mars-bluetooth-hci](https://github.com/Metirionic/mars-bluetooth-hci) — COBS
  serialization library.
- [nRF Connect SDK](https://developer.nordicsemi.com/nRF_Connect_SDK/) —
  controller-owned `cs_antenna_switch` binding.
