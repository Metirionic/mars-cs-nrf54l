# Hardware reference

This is a reference for the board overlays and antenna/path presets used by
this firmware. To build from source, see
[docs/build-from-source.md](build-from-source.md); to flash prebuilt firmware,
see [docs/flash-quickstart.md](flash-quickstart.md).

This document is a reference table only — it does not cover physical antenna
wiring, RF placement, antenna tuning, the COBS wire format, or the
mars-bluetooth-hci API.

## Supported boards

Seven of the target boards build for `BOARD=nrf54l15dk/nrf54l15/cpuapp` with the
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
| Raytac AN54LV-K15 | `boards/raytac_an54lv_k15_nrf54l15_cpuapp.overlay` | `uart20` @ 921600 | — (RTT via debug probe) | `P0.03`, `P0.04` |
| Insight SiP ISP2454 | `boards/insight_isp2454_nrf54l15_cpuapp.overlay` | `uart20` @ 921600 | — (RTT via onboard J-Link OB) | — (no antenna-switch node) |

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
  On boards with a console UART (all except the TAG, the ME54BE01, the
  AN54LV-K15, and the ISP2454), the console UART also
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

### Raytac AN54LV-K15 wiring notes

The AN54LV-K15 is a Raytac module dev board (nRF54L15-based AN54LV-K15 module).
Like the ME54BE01 it exposes a single UART, runs the console over RTT, and has
no onboard debugger — but it is the one carrier board here with a
three-antenna RF front end: a SKY13586-678LF SP3T switch selecting between
three RF connectors. It builds on `BOARD=nrf54l15dk/nrf54l15/cpuapp` (there is
no NCS board def for the AN54LV-K15) — the overlay selects the carrier.

- **No onboard debugger.** The AN54LV-K15 EVB has no onboard J-Link; flashing
  and RTT both go through an nRF54L15 DK's `DEBUG OUT` header (the DK's onboard
  debugger is rerouted to the module's SoC).
- **Single UART; console over RTT.** Only `uart20` is used — the COBS ranging
  stream via `cobs-uart`. There is no console/shell UART: the overlay binds
  only `cobs-uart = &uart20` and `boards/raytac_an54lv_k15.conf` selects the
  RTT console backend (and disables the DK defconfig's `CONFIG_UART_CONSOLE`,
  since the overlay defines no `zephyr,console` node), so the console (log
  output) runs over Segger RTT via the same debug probe, with no extra wiring.
- **No console for non-Raytac builds.** A preset whose conf does not select the
  RTT console keeps the DK defconfig's console on `uart30` — pins that go
  nowhere on the AN54LV-K15 EVB — so a foreign build flashed onto this carrier
  boots and runs with no console of any kind. To see its logs, add the RTT
  override as an extra `EXTRA_CONF_FILE` — the same lines
  `boards/raytac_an54lv_k15.conf` carries: `CONFIG_USE_SEGGER_RTT=y`,
  `CONFIG_RTT_CONSOLE=y`, `CONFIG_UART_CONSOLE=n` — which restores the log
  console over the DEBUG-OUT probe (bench-verified with an
  `nrf54l15dk_cent_a1_4` build: boot banner and live CS lines over RTT).
- **Both roles log over RTT; a "silent" build usually means a stale ring.**
  Both Raytac presets write their console through the DEBUG-OUT probe —
  including the reflector: a bench-verified `raytac_an54lv_k15_peri_a3_4`
  fresh boot logs the banner, SDC revision, `Connected to …`, `CS capability
  exchange completed (peer antennas: 1)` and the full `CS config creation
  complete` block over RTT, same shape as the initiator.
- **RTT reads a stale ring after a reflash — invalidate the control block
  first.** The console's RTT init runs in `STRONG_CHECK` mode: a valid RAM
  control block survives both a system reset and a reflash (RAM is not
  cleared by `SYSRESETREQ` or by `nrfutil device program`), and the 1 KB
  up-buffer runs `NO_BLOCK_SKIP` — once full, further output is silently
  discarded instead of wrapping. Post-hoc reads (`JLinkExe` `savebin` at the
  `_SEGGER_RTT` control block, no RTT client draining it) then show whatever
  the last drained session left: `wr` frozen, "zero bytes" for any image
  flashed on top, or two sessions' content appended together. This is
  build-independent and cost bench verification of the reflector console a
  long misdiagnosis. Clean-read recipe: with the probe attached, corrupt the
  control-block ID (e.g. `JLinkExe`: `h`,
  `w1 <_SEGGER_RTT addr>, 0xAA, 2` clobbering the `SE` of `"SEGGER RTT"`),
  then `r` — `g`; the fresh boot sees an invalid control block and re-inits
  `wr` = `rd` = 0, and the ring's first ~1 KB (banner through early session)
  is this boot's output only. Power-cycling the EVB works too (RAM clears)
  but on this bench the power source is the FT232 on J1 — unplugging it is
  the console's power switch.
- **COBS UART.** Unlike the ME54BE01 there is no onboard USB-to-serial bridge:
  header J1 carries `uart20` (TX `P1.04` / RX `P1.05`, inherited from the
  nrf54l15dk base DTS, `921600` baud, 8N1, no flow control), read through an
  external UART-to-USB adapter (e.g. FT232) — the same external-adapter shape
  as the TAG.
- **Power feeds from the FT232 over J1.** The same J1 header that taps
  `uart20` also carries the carrier's power feed from the external adapter —
  with the FT232 on J1, the EVB is powered through it, and Type-C/battery are
  not in this bench's power path. Keep J1 single-source: a second power source
  alongside the adapter backfeeds the rail. The carrier's CS-active current
  rides on the FT232's supply too, so the adapter and its USB source are the
  EVB's only supply.
- **Antenna switch and the antenna-index ↔ RF-connector mapping.** The
  SKY13586-678LF SP3T switch has two control inputs wired to `V1` → `P0.03`,
  `V2` → `P0.04`. The overlay encodes them as `ant-gpios`: `&gpio0 4` first
  (bit 0 = `V2`), `&gpio0 3` second (bit 1 = `V1`), `multiplexing-mode = <1>`.
  The switch truth table gives `(V1,V2)` = `(0,0)` → RF3, `(x,1)` → RF2,
  `(1,0)` → RF1, so antenna index *i* (the `cs_antenna_switch.c` binary index)
  selects **RF(3−i)**: 0 → RF3, 1 → RF2, 2 → RF1. Because index 0 must drive
  all control pins low and `(0,0)` selects RF3, no GPIO ordering yields a linear
  0 = RF1 mapping — the order above is the clean inverse. Beware: the NCS CS
  documentation's multiplexing-mode-1 truth table is transposed relative to the
  `cs_antenna_switch.c` implementation — derive wiring from the code, as this
  overlay does.
- **Path → RF-connector mapping (N_AP = 3).** A `*_a3_4` preset runs
  `NUM_ANTENNAS=3`, so CS steps report up to three antenna paths. The logical
  path → RF-connector view composes the mars-bluetooth-hci antenna-permutation
  tables (`antenna_permutation::lookup(3, antenna_permutation_index)` — logical
  path → 0-based antenna index; `An_Bm` per-step `antenna_permutation_index`)
  with the RF(3−i) board mapping above:

  | `antenna_permutation_index` (0–5) | path 0 | path 1 | path 2 |
  |-----------------------------------|--------|--------|--------|
  | 0 | RF3 | RF2 | RF1 |
  | 1 | RF2 | RF3 | RF1 |
  | 2 | RF3 | RF1 | RF2 |
  | 3 | RF1 | RF3 | RF2 |
  | 4 | RF1 | RF2 | RF3 |
  | 5 | RF2 | RF1 | RF3 |

- **Switch control levels.** The SKY13586's control inputs are referenced to
  the switch's own VDD (logic high 1.6–3.6 V), while the nRF54L15 GPIO high
  sits at roughly its supply minus 0.4 V — the supply rail feeding the switch's
  VDD and the `P0.03`/`P0.04` lines is a wiring-rig check, alongside the
  physical path → RF-connector ordering above. See the
  [Raytac AN54LV module documentation](https://www.raytac.com/product/ins.php?index_id=169)
  and the [SKY13586-678LF data sheet](https://www.skyworksinc.com/-/media/SkyWorks/Documents/Products/2401-2500/SKY13586-678LF_203452G.pdf)
  (control truth table).

### Insight SiP ISP2454 wiring notes

The ISP2454 is an Insight SiP module dev kit (order code ISP2454-LX-EB: the
plug-on ISP2454-LX-TB Test Board carrying the ISP2454-LX module, stacked on
the ISP130603 Interface Board). Like the ME54BE01 it exposes a single module
UART and runs the console over RTT — but unlike it the debugger is onboard.
It builds on `BOARD=nrf54l15dk/nrf54l15/cpuapp` (there is no NCS board def
for the ISP2454 EVK) — the overlay selects the carrier, and the EVK's own
flash flow (Insight AN250502) is the standard Nordic Recover/Flash against
that same board target.

- **Onboard J-Link OB.** The ISP130603 Interface Board integrates a J-Link OB
  JTAG/SWD emulator: its single USB connector both flashes the module and
  reads the RTT console — no external debug probe needed (unlike the TAG,
  ME54BE01, and AN54LV-K15).
- **Bench: flash and RTT via `nrfutil` — the J-Link DLL is unusable on this
  OB.** `JLinkExe`/`JLinkRTTLogger` (system DLL 7.92m) SIGABRT with
  `bit out of range 0 - FD_SETSIZE` on every attach, re-attempting an
  OB firmware update that never sticks; `nrfutil device` (recover, program,
  read, write) performs the same operations over the same OB cleanly.
- **Bench: AP-Protect ships enabled — recover once before flashing.** The
  first `nrfutil device program` against the EVK fails with
  "Application core access port is currently closed"; a one-time
  `nrfutil device recover` clears it, and it stays clear for later flashes.
- **Single UART; console over RTT.** Only `uart20` is exposed — the COBS
  ranging stream via `cobs-uart`, on Interface-Board silk `P36` (module TX,
  `P1.04`) and `P37` (module RX, `P1.05`, `921600` baud, 8N1, no flow
  control). The pinctrl is inherited from the nrf54l15dk base DTS, and the
  EVK documents exactly this external-adapter path (Insight AN250502 wires an
  external USB-UART adapter to P36/P37 for the module's log UART). There is
  no console/shell UART: the overlay binds only `cobs-uart = &uart20` and
  `boards/insight_isp2454.conf` selects the RTT console backend (and disables
  the DK defconfig's `CONFIG_UART_CONSOLE`, since the overlay defines no
  `zephyr,console` node), so the console (log output) runs over Segger RTT
  via the same onboard J-Link OB. (The DK-default uart30 console pins
  `P0.00`/`P0.01` are not routed to the Interface Board; a uart30 console
  would need Insight's `P0.02`/`P0.03` pinctrl remap — RTT avoids it.)
- **Bench: RTT via `nrfutil device read` — drain the stale boot ring first.**
  The console's 1 KB up-buffer runs `NO_BLOCK_SKIP` (the same stale-ring
  behavior noted for the AN54LV-K15 above) and fills during boot, so a
  post-hoc read returns a frozen tail or nothing. The control block sits at
  the ELF symbol `_SEGGER_RTT` (up-buffer behind it); read RAM at that
  address via `nrfutil device read`, and to tail live output write
  `RdOff = WrOff` at the control block (`nrfutil device write`) so the
  stale boot output drains.
- **Bench: serial-port identities.** The OB exposes a single CDC (`ttyACM0`,
  silent — the module's `uart20` never reaches the OB, so the ranging stream
  never appears on the J-Link's port), and `nrfutil device list` mislabels
  the external FT232's `/dev/ttyUSB0` as this J-Link's `vcom 1`; disambiguate
  via `/dev/serial/by-id` (`usb-FTDI_FT232R…` → `ttyUSB0`). The FT232 at
  921600 on `P36`/`P37` is verified clean — a 4 MB soak with zero COBS decode
  failures past the startup partial.
- **The module rail is 3.0 V, not the DK's 1.8 V.** With the Interface
  Board's default power path (`J4` = `REG`, the embedded 3 V regulator), the
  module runs at 3.0 V (its allowed range is 1.7–3.6 V), so header IO is
  3.0 V nominal. Leave the external adapter's VCC unconnected (GND + TX/RX
  only): supply pins ride the stacking and GPIO headers with no series
  isolation, so a powered adapter backfeeds the module rail — the same
  backfeed risk the Raytac jig hit.
- **LF clock: LFXO load clamped to the SoC maximum.** The ISP2454-LX module's
  32.768 kHz crystal is specified for a 19 pF load, and Insight's firmware
  note (data sheet §2.5, note 2) prescribes `load-capacitance-femtofarad =
  <19000>` — but the nRF54L15 cannot express that: the product spec holds the
  internal LFXO capacitor bank to 4–18 pF in 0.5 pF steps, and the SoC code
  (`soc/nordic/nrf54l/soc.c`) `BUILD_ASSERT`s 3000–18000 fF, so a 19000
  overlay fails the build. The overlay sets the SoC maximum `18000` — the
  nearest expressible load — against the DK target's `17000`; the residual
  1 pF delta is a bench-timing watch item — the four role/mode bench soaks
  showed no drift signature — not tuning guidance (the load capacitors are
  internal on both boards).
- **Antenna.** A single integrated PCB antenna, no RF switch and no populated
  RF-connector path (the Test Board's SMA sits behind 0 Ω links — external
  conducted-RF access is rework-only). Same single-antenna shape as Fanstel
  and the ME54BE01, so all ISP2454 presets use `NUM_ANTENNAS=1`
  (`4_path_1_local.conf`) and the overlay carries no `cs_antenna_switch`
  node.
- **Engineering-B die.** These EVKs ship on nRF54L15 Engineering-B silicon
  (the vendor's own EB-listing warning) — worth confirming the die revision
  when comparing CS results across carrier boards; the bench rig's EB reads
  REV1 (`nrfutil device info` → `NRF54L15_xxxx_REV1`). See the
  [ISP2454 DK data sheet](https://www.insightsip.com/fichiers_insightsip/pdf/ble/ISP2454/isp_ble_DS2454_DK.pdf)
  and [AN250502, "Use of the ISP2454-LX Development Kit"](https://www.insightsip.com/fichiers_insightsip/pdf/ble/ISP2454/isp_ble_AN250502.pdf).

### Antenna-switch node

- The `cs_antenna_switch` node (`compatible = "nordic,bt-cs-antenna-switch"`,
  `multiplexing-mode = <1>` — the TAG sets `<0>`) is owned and consumed internally by the Nordic
  controller library in NCS; no code in this repo reads `ant-gpios` directly.
  The overlay comment "See `cs_antenna_switch.c`" refers to NCS-owned source, not
  a file in this repo.
- Fanstel, the Minewsemi ME54BE01, and the Insight SiP ISP2454 have no
  `cs_antenna_switch` node (single-antenna boards; the Nordic controller's
  `cs_antenna_switch.c` is compiled only under
  `CONFIG_BT_CTLR_SDC_CS_MULTIPLE.ANTENNA_SUPPORT`, i.e. `NUM_ANTENNAS >= 2`,
  and all three boards' presets use `NUM_ANTENNAS=1`). The
  Fanstel overlay instead sets `&lfxo` load-capacitance to 15.5 pF (`15500` fF) —
  a factual board-clock difference recorded here as overlay content, not tuning
  guidance.

## Antenna and path presets

### What "A1 2-path" and "A2 4-path" mean

"A1" / "A2" is the antenna count, set by `CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS`.
"2-path" / "4-path" is the antenna-path count, set by
`CONFIG_BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS` (mirrored on the host side by
`CONFIG_BT_RAS_MAX_ANTENNA_PATHS`). So "A1 2-path" means 1 antenna, 2 paths;
"A2 4-path" means 2 antennas, 4 paths.

### Kconfig fragments

Every preset pulls a `boards/*_local.conf` path-local fragment. RAS presets pull
just that one (plus `central.overlay` for initiators, and the carrier
`boards/<carrier>.conf` fragment wherever a board's RTT-console/serial-driver
Kconfig is needed — see each carrier's wiring notes). Each local fragment is
three lines setting the same three symbols:

| Fragment | `RAS_MAX_ANTENNA_PATHS` | `SDC_CS_MAX_ANTENNA_PATHS` | `SDC_CS_NUM_ANTENNAS` | Antennas / Paths |
|----------|------------------------|----------------------------|------------------------|------------------|
| `2_path_1_local.conf` | 2 | 2 | 1 | A1 / 2-path |
| `2_path_2_local.conf` | 2 | 2 | 2 | A2 / 2-path |
| `4_path_1_local.conf` | 4 | 4 | 1 | A1 / 4-path |
| `4_path_2_local.conf` | 4 | 4 | 2 | A2 / 4-path |
| `4_path_3_local.conf` | 4 | 4 | 3 | A3 / 4-path |
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
IPT presets additionally pull the `inline_pct_*.conf` fragments above. IPT
covers all eight carrier boards — A1/A2 4-path on the initiator, A1 4-path on the
reflector, with the TAG and the Ezurio reflector at A2 4-path and the Raytac at
A3 4-path.

| Preset | Mode | Role | Board | Overlay | `EXTRA_CONF_FILE` | Antennas / Paths |
|--------|------|------|-------|---------|-------------------|------------------|
| `nrf54l15dk_cent_a1_2` | RAS | initiator | nRF54L15DK | `nrf54l15dk_*.overlay` | `central.overlay;2_path_1_local.conf` | A1 / 2 |
| `nrf54l15dk_cent_a1_4` | RAS | initiator | nRF54L15DK | `nrf54l15dk_*.overlay` | `central.overlay;4_path_1_local.conf` | A1 / 4 |
| `nrf54l15dk_cent_a4_4` | RAS | initiator | nRF54L15DK | `nrf54l15dk_*.overlay` | `central.overlay;4_path_4_local.conf` | A4 / 4 |
| `nrf54l15dk_cent_a1_4_ipt` | IPT | initiator | nRF54L15DK | `nrf54l15dk_*.overlay` | `central.overlay;inline_pct_initiator.conf;inline_pct_shared.conf;4_path_1_local.conf` | A1 / 4 |
| `nrf54l15tag_cent_a2_4` | RAS | initiator | nRF54L15 TAG | `nrf54l15tag_*.overlay` | `central.overlay;4_path_2_local.conf;nrf54l15tag.conf` | A2 / 4 |
| `nrf54l15tag_cent_a2_4_ipt` | IPT | initiator | nRF54L15 TAG | `nrf54l15tag_*.overlay` | `central.overlay;inline_pct_initiator.conf;inline_pct_shared.conf;4_path_2_local.conf;nrf54l15tag.conf` | A2 / 4 |
| `ublox_cent_a1_4` | RAS | initiator | U-Blox NINA-B40 | `ublox_*.overlay` | `central.overlay;4_path_1_local.conf` | A1 / 4 |
| `ublox_cent_a1_4_ipt` | IPT | initiator | U-Blox NINA-B40 | `ublox_*.overlay` | `central.overlay;inline_pct_initiator.conf;inline_pct_shared.conf;4_path_1_local.conf` | A1 / 4 |
| `ezurio_bl54l15u_cent_a2_4` | RAS | initiator | Ezurio BL54L15u | `ezurio_*.overlay` | `central.overlay;4_path_2_local.conf` | A2 / 4 |
| `ezurio_bl54l15u_cent_a2_4_ipt` | IPT | initiator | Ezurio BL54L15u | `ezurio_*.overlay` | `central.overlay;inline_pct_initiator.conf;inline_pct_shared.conf;4_path_2_local.conf` | A2 / 4 |
| `fanstel_bm15c_cent_a1_4` | RAS | initiator | Fanstel BM15C | `fanstel_*.overlay` | `central.overlay;4_path_1_local.conf` | A1 / 4 |
| `fanstel_bm15c_cent_a1_4_ipt` | IPT | initiator | Fanstel BM15C | `fanstel_*.overlay` | `central.overlay;inline_pct_initiator.conf;inline_pct_shared.conf;4_path_1_local.conf` | A1 / 4 |
| `minew_me54be01_cent_a1_4` | RAS | initiator | Minewsemi ME54BE01 | `minew_me54be01_*.overlay` | `central.overlay;4_path_1_local.conf;minew_me54be01.conf` | A1 / 4 |
| `minew_me54be01_cent_a1_4_ipt` | IPT | initiator | Minewsemi ME54BE01 | `minew_me54be01_*.overlay` | `central.overlay;inline_pct_initiator.conf;inline_pct_shared.conf;4_path_1_local.conf;minew_me54be01.conf` | A1 / 4 |
| `raytac_an54lv_k15_cent_a3_4` | RAS | initiator | Raytac AN54LV-K15 | `raytac_an54lv_k15_*.overlay` | `central.overlay;4_path_3_local.conf;raytac_an54lv_k15.conf` | A3 / 4 |
| `raytac_an54lv_k15_cent_a3_4_ipt` | IPT | initiator | Raytac AN54LV-K15 | `raytac_an54lv_k15_*.overlay` | `central.overlay;inline_pct_initiator.conf;inline_pct_shared.conf;4_path_3_local.conf;raytac_an54lv_k15.conf` | A3 / 4 |
| `insight_isp2454_cent_a1_4` | RAS | initiator | Insight SiP ISP2454 | `insight_isp2454_*.overlay` | `central.overlay;4_path_1_local.conf;insight_isp2454.conf` | A1 / 4 |
| `insight_isp2454_cent_a1_4_ipt` | IPT | initiator | Insight SiP ISP2454 | `insight_isp2454_*.overlay` | `central.overlay;inline_pct_initiator.conf;inline_pct_shared.conf;4_path_1_local.conf;insight_isp2454.conf` | A1 / 4 |
| `nrf54l15dk_peri_a1_4` | RAS | reflector | nRF54L15DK | `nrf54l15dk_*.overlay` | `4_path_1_local.conf` | A1 / 4 |
| `nrf54l15dk_peri_a2_2` | RAS | reflector | nRF54L15DK | `nrf54l15dk_*.overlay` | `2_path_2_local.conf` | A2 / 2 |
| `nrf54l15dk_peri_a4_4` | RAS | reflector | nRF54L15DK | `nrf54l15dk_*.overlay` | `4_path_4_local.conf` | A4 / 4 |
| `nrf54l15dk_peri_a1_4_ipt` | IPT | reflector | nRF54L15DK | `nrf54l15dk_*.overlay` | `inline_pct_reflector.conf;inline_pct_shared.conf;4_path_1_local.conf` | A1 / 4 |
| `nrf54l15tag_peri_a2_4` | RAS | reflector | nRF54L15 TAG | `nrf54l15tag_*.overlay` | `4_path_2_local.conf;nrf54l15tag.conf` | A2 / 4 |
| `nrf54l15tag_peri_a2_4_ipt` | IPT | reflector | nRF54L15 TAG | `nrf54l15tag_*.overlay` | `inline_pct_reflector.conf;inline_pct_shared.conf;4_path_2_local.conf;nrf54l15tag.conf` | A2 / 4 |
| `ezurio_bl54l15u_peri_a2_4` | RAS | reflector | Ezurio BL54L15u | `ezurio_*.overlay` | `4_path_2_local.conf` | A2 / 4 |
| `ezurio_bl54l15u_peri_a2_4_ipt` | IPT | reflector | Ezurio BL54L15u | `ezurio_*.overlay` | `inline_pct_reflector.conf;inline_pct_shared.conf;4_path_2_local.conf` | A2 / 4 |
| `fanstel_bm15c_peri_a1_4` | RAS | reflector | Fanstel BM15C | `fanstel_*.overlay` | `4_path_1_local.conf` | A1 / 4 |
| `fanstel_bm15c_peri_a1_4_ipt` | IPT | reflector | Fanstel BM15C | `fanstel_*.overlay` | `inline_pct_reflector.conf;inline_pct_shared.conf;4_path_1_local.conf` | A1 / 4 |
| `ublox_peri_a1_4` | RAS | reflector | U-Blox NINA-B40 | `ublox_*.overlay` | `4_path_1_local.conf` | A1 / 4 |
| `ublox_peri_a1_4_ipt` | IPT | reflector | U-Blox NINA-B40 | `ublox_*.overlay` | `inline_pct_reflector.conf;inline_pct_shared.conf;4_path_1_local.conf` | A1 / 4 |
| `minew_me54be01_peri_a1_4` | RAS | reflector | Minewsemi ME54BE01 | `minew_me54be01_*.overlay` | `4_path_1_local.conf;minew_me54be01.conf` | A1 / 4 |
| `minew_me54be01_peri_a1_4_ipt` | IPT | reflector | Minewsemi ME54BE01 | `minew_me54be01_*.overlay` | `inline_pct_reflector.conf;inline_pct_shared.conf;4_path_1_local.conf;minew_me54be01.conf` | A1 / 4 |
| `raytac_an54lv_k15_peri_a3_4` | RAS | reflector | Raytac AN54LV-K15 | `raytac_an54lv_k15_*.overlay` | `4_path_3_local.conf;raytac_an54lv_k15.conf` | A3 / 4 |
| `raytac_an54lv_k15_peri_a3_4_ipt` | IPT | reflector | Raytac AN54LV-K15 | `raytac_an54lv_k15_*.overlay` | `inline_pct_reflector.conf;inline_pct_shared.conf;4_path_3_local.conf;raytac_an54lv_k15.conf` | A3 / 4 |
| `insight_isp2454_peri_a1_4` | RAS | reflector | Insight SiP ISP2454 | `insight_isp2454_*.overlay` | `4_path_1_local.conf;insight_isp2454.conf` | A1 / 4 |
| `insight_isp2454_peri_a1_4_ipt` | IPT | reflector | Insight SiP ISP2454 | `insight_isp2454_*.overlay` | `inline_pct_reflector.conf;inline_pct_shared.conf;4_path_1_local.conf;insight_isp2454.conf` | A1 / 4 |

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
(the role fragment `;`-separated from the path-local fragment; the TAG, ME54BE01,
AN54LV-K15, and ISP2454 presets also append their `boards/<carrier>.conf`
fragment for RTT-console/serial-driver Kconfig).
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

The firmware maps the antenna counts to a BLE CS tone-antenna configuration
(`An_Bm`) in `common/antenna.c`. The lookup is **role-aware**:
`antenna_get_config_for_role(role)` takes the local antenna count from
`CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS` and the peer count from the CS
capabilities exchange — `remote_capabilities_cb` stores the peer's
`num_antennas_supported` via `antenna_set_peer_count()` — then indexes
`ANTENNA_MAPPING[dev_a - 1][dev_b - 1]` with the local and peer counts swapped
by role:

- **Initiator** — `antenna_get_config_from_ab(local, peer)`:
  `ANTENNA_MAPPING[NUM_ANTENNAS - 1][peer - 1]`
- **Reflector** — `antenna_get_config_from_ab(peer, local)`:
  `ANTENNA_MAPPING[peer - 1][NUM_ANTENNAS - 1]`

Before the capabilities exchange completes (or if it fails), the peer count
falls back to the heuristic `peer = CONFIG_BT_CTLR_SDC_CS_MAX_ANTENNA_PATHS /
CONFIG_BT_CTLR_SDC_CS_NUM_ANTENNAS` (integer division). For a **matched** pair
the negotiated count equals the heuristic, so the table below still holds; for
a **mismatched** pair the negotiated count wins — e.g. an Ezurio A2/4
initiator paired with a DK A1/4 reflector negotiates `A2_B1` on both sides
(initiator `preferred_peer_antenna = 1`, reflector `= 3`), not the heuristic's
`A2_B2` / `A4_B1` (which the controller rejects with `0x0d`).

The same `(antennas, paths)` config can therefore resolve to a different `An_Bm`
depending on which role runs it — the local and peer counts land in opposite
index slots. For every config the [Presets](#presets) table ships this resolves
to:

| Antennas / Paths | Initiator `An_Bm` | Reflector `An_Bm` |
|------------------|-------------------|--------------------|
| A1 / 2           | `A1_B2`           | —                  |
| A2 / 2           | —                 | `A1_B2`            |
| A1 / 4           | `A1_B4`           | `A4_B1`            |
| A2 / 4           | `A2_B2`           | `A2_B2`            |
| A3 / 4           | `A3_B1`           | `A1_B3`            |
| A4 / 4           | `A4_B1`           | `A1_B4`            |

A `—` marks a role no shipped preset runs in that config (A1 2-path ships only
as initiator; A2 2-path only as reflector). A2 4-path is symmetric — `A2_B2`
either way. The `ANTENNA_MAPPING` table also carries placeholder `A1_B1` entries
for index combinations no shipped preset uses (e.g. `[1][2]`, `[2][1]`, `[3][1]`);
only the six configs above actually ship.

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
