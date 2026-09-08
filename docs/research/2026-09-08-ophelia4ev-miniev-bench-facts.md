# Würth Elektronik Ophelia-IV MiniEV: bench and board-def facts

Research asset for [wayfinder #178](https://github.com/Metirionic/mars-cs-nrf54l/issues/178)
(support destination: `ophelia4ev_{cent,peri}_a1_4{,_ipt}` presets, capture-path bench proof
in [#179](https://github.com/Metirionic/mars-cs-nrf54l/issues/179)).

Sources (fetched 2026-09-08): Würth Elektronik user manual "Mini evaluation board for radio
module Ophelia-IV", order code 2621119022001, Version 1.0, December 9, 2025, 37 pp. —
downloaded from the we-online.com manual URL; Würth application note ANR036 v1.1 (August
2025, "Build your own firmware — getting started with Zephyr"); the NCS v3.4.0 Zephyr tree
(`/home/aro/work/zephyr/`); and the 2026-09-08 charting recon. Every claim is tagged **EV**
(MiniEV manual), **AN** (ANR036), **BENCH** (charting recon — hardware-observed during
charting and carried forward, not re-verified here); Nordic-side claims are tagged **NCS**
and are not passed off as Würth facts.

Note on the local PDF: the repo working tree's untracked `ophelia_miniev.pdf` is **not a
PDF** — it is a 13 KB saved-browser-page HTML wrapper around the Chrome PDF viewer, with the
real manual URL embedded in it (`https://www.we-online.com/components/products/manual/
UM_Ophelia-IV_MiniEV_262111902xxxx%20(rev1.0).pdf`). The manual used here was fetched from
that URL and is cited, not committed (the KAGA research-doc precedent committed no vendor
PDFs either).

## TL;DR

The gating question — **is there an NCS board def for this carrier, and does it fit the CS
apps** — has a mostly-friendly answer: NCS v3.4.0 ships `boards/we/ophelia4ev` (targets
`ophelia4ev/nrf54l15/cpuapp` + `ophelia4ev/nrf54l15/cpuflpr`), and Würth's own app note
blesses the MiniEV on it — ANR036 v1.1 §3 lists "Ophelia-IV Mini-EV (2621119022001) …
Available since Zephyr 4.2 under ./zephyr/boards/we/ophelia4ev" (AN). The def's `uart20`
pinctrl (TX `P1.04`, RX `P1.15`) matches the MiniEV CON4 FTDI wiring pin-for-pin (EV Table
13 vs NCS `ophelia4ev_nrf54l15-pinctrl.dtsi:8-28`), the partition layout is the shared
Nordic vendor file (identical to the DK), and GRTC channel ownership is identical to the
DK. So the carrier support can build against its own board def — the `nrf54l15tag`
precedent generalizes — rather than DK+overlay.

Two deltas matter before any preset is written:

1. **The board defconfig selects the internal RC oscillator for the 32 K source**
   (`CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y`, NCS `ophelia4ev_nrf54l15_cpuapp_defconfig:18`)
   where the Zephyr default — and the DK, which does not override — is the crystal
   (NCS: `drivers/clock_control/Kconfig.nrf:35`). The MiniEV ships **without** an LFXO
   crystal (Q1/C9/C10 not assembled, R1/R2 0 Ω fitted, EV §5.6.2 + BOM), so RC is the
   honest default for this carrier: **do not copy the DK's XTAL default** without also
   soldering the crystal rework.
2. **All five console chosen nodes point at `uart20`** (`zephyr,console`, `shell-uart`,
   `uart-mcumgr`, `bt-mon-uart`, `bt-c2h-uart` — NCS `ophelia4ev_nrf54l15_cpuapp.dts:16-20`)
   — the same `uart20` the COBS overlay binds — and `uart30` is **disabled** on the cpuapp
   target. The raytac/minew-shape RTT-console carrier conf is therefore mandatory;
   the defconfig's `UART_CONSOLE=y` default would otherwise interleave log bytes into the
   COBS stream.

Also: the def's LEDs, buttons, `mx25r64` SPI flash, and NFC node have **no counterpart on
the MiniEV** (schematic/BOM) — phantom nodes for this carrier, with the `mx25r64` driver
probing a floating SPI bus at boot.

## 1. Which physical board the `ophelia4ev` def models

- **WE's own mapping is MiniEV → `ophelia4ev`.** ANR036 v1.1 §3 (p.6) lists the supported
  platforms: "Ophelia-IV Mini-EV (2621119022001) — Radio module Ophelia-IV … using chipset
  nRF54L15 — Available since Zephyr 4.2 under ./zephyr/boards/we/ophelia4ev", with
  `samples/bluetooth/peripheral` as the recommended example (AN §5.2 Table 3). ANR036
  misprints the module order code as 2621119022001; the module is 2621011022000 (EV §3
  Table 3, "BYOF radio module with DTM (Direct Test Mode) firmware").
- **The NCS v3.4.0 tree carries the def, tracked and clean** (NCS:
  `/home/aro/work/zephyr/boards/we/ophelia4ev/`, 14 files; last touched by the v3.4.0
  fromtree sync bf801e4e3d1, 2026-07-01; `git status` clean). ANR036's "since Zephyr 4.2"
  matches the NCS v3.4.0 Zephyr base.
- **The def was written for the shared platform, not only the MiniEV.** `board.yml` says
  `full_name: Ophelia-IV DK` (NCS: `board.yml:3`), upstream Zephyr's board page instructs
  "connect the OPHELIA-IV EV to your computer using the USB port on the board" (NCS:
  `doc/index.rst:54`), and the WE catalog lists the MiniEV (2621119022001) as the family's
  only EV product. The MiniEV has no USB port, no LEDs beyond a power LED, one reset button,
  and no SPI flash (EV Figure 2, §5.9 BOM). Treat the doc's USB sentence and the def's
  peripheral set as shared-platform content; the parts below are what actually maps.

  | Board-def node (NCS) | MiniEV reality (EV) |
  |---|---|
  | `led0-3`: `P1.09`, `P1.11`, `P2.00`, `P1.10` (`ophelia4ev_common.dtsi:10-32`) | No LEDs (only `LED_POWER` on the 5 V rail, EV BOM); pins are P3/P1 header pads (EV Tables 7, 9) on unmounted headers |
  | buttons `sw0-sw3`: `P2.03`, `P1.12`, `P1.13`, `P1.14` (`ophelia4ev_common.dtsi:48-74`) | No buttons except S1 = /RESET (EV §5.5); the four pins are header pads only |
  | `mx25r64` SPI NOR on `spi00`, SCK `P2.01`/MOSI `P2.02`/MISO `P2.04`, CS `P2.05`, RST `P2.06` (`ophelia4ev_nrf54l15_cpuapp.dts:122-149`) | No flash part on the board (EV §5.7 schematic, §5.9 BOM — no MX25R entry); the pins are P1/P2 header pads. `status = "okay"` means the driver probes a floating bus at boot |
  | `&nfct` okay (NFC pads) | No NFC antenna on the MiniEV |
  | `uart20` TX `P1.04` / RX `P1.15` / RTS `P1.06` / CTS `P1.07` (`ophelia4ev_nrf54l15-pinctrl.dtsi:7-28`) | **Matches CON4 exactly** — the FTDI cable map of EV §5.3.7 and Figure 12 |
  | `uart30` TX `P0.00` / RX `P0.01` (`pinctrl.dtsi:30-47`) | Pins land on unmounted P1 header pins 5/13 (EV Table 7); `uart30` is not enabled on the cpuapp target anyway |
  | `&grtc` owned 0–11, child 3,4,7–11 (`cpuapp.dts:59-66`) | Identical to the DK (`nrf54l_05_10_15_cpuapp_common.dtsi`) — no SDC-side delta |
  | `&lfxo` internal, 17000 fF; `&hfxo` internal, 15000 fF (`cpuapp.dts:39-47`) | LFXO crystal not fitted as shipped (EV §5.6.2); the defconfig runs the 32 K source on RC regardless |

  The DK uses `P1.09`/`P1.10` as its antenna-switch pins (`docs/hardware.md`); on this def
  those are `led0`/`led3`. Irrelevant for `a1_4` presets (`NUM_ANTENNAS=1` — no
  `cs_antenna_switch` node), but worth knowing if the antenna story ever changes.

## 2. Board-def facts (NCS v3.4.0)

### Targets

- `ophelia4ev/nrf54l15/cpuapp` — ARM Cortex-M33, `sysbuild: true`, **ram 188 / flash 1428**
  (KB) — byte-identical resource figures to `nrf54l15dk/nrf54l15/cpuapp` (NCS:
  `ophelia4ev_nrf54l15_cpuapp.yaml`, `nrf54l15dk_nrf54l15_cpuapp.yaml`). Same silicon
  budget the CS apps already run against (the 188 KB RAM heap work of issue #174 included).
- `ophelia4ev/nrf54l15/cpuflpr` — RISC-V FLPR sibling, 96/96, `CONFIG_XIP=n` (runs from
  SRAM), console on `uart30`, needs sysbuild with `vpr_launcher` (NCS:
  `ophelia4ev_nrf54l15_cpuflpr.*`, `doc/index.rst:36-43`). **Out of scope** for the CS
  presets — noted here so the target's existence does not surprise anyone.

### Runners

- `board.cmake` includes the DK's runner config verbatim (NCS:
  `boards/we/ophelia4ev/board.cmake:4` → `boards/nordic/nrf54l15dk/board.cmake`), so:
  - **jlink**: `--device=nRF54L15_M33 --speed=4000` for the cpuapp target (and
    `--device=nRF54L15_RV32` for the FLPR) (NCS: DK `board.cmake:4-9`).
  - **nrfutil**: registered with **no default arguments** (NCS:
    `boards/common/nrfutil.board.cmake:4-5`), and included **before** the jlink file, so
    `west flash` defaults to the nrfutil runner; `west debug` defaults to jlink. The
    runner auto-derives `--x-family nrf54l` from the SoC (NCS:
    `scripts/west_commands/runners/nrf_common.py:216-233`).
  - West-side runner options worth knowing (NCS: `runners/nrf_common.py`): `--dev-id`
    (repeatable probe serial), `--erase-mode` with choices `none|ranges|all` (the
    `chip_erase_mode` equivalent — `ranges` is the safe value), `--ext-erase-mode`,
    `--recover`, `--softreset`/`--pinreset`, `--force`, `--tool-opt`.
  - The bench-verified flash flow below drives `nrfutil device program` directly (the
    `nrfjprog` tool is not installed here); the runner facts above are for `west flash`.

### Clock, regulators, GRTC

- **32 K source: RC by default.** `CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y` in the board
  defconfig (NCS: `ophelia4ev_nrf54l15_cpuapp_defconfig:18`), overriding the Zephyr default
  `CLOCK_CONTROL_NRF_K32SRC_XTAL` (NCS: `drivers/clock_control/Kconfig.nrf:35`). The DK
  sets nothing and gets XTAL. The upstream board doc states the same in prose: "The
  Ophelia-IV uses the internal low frequency RC oscillator" (NCS: `doc/index.rst:16-17`).
  EV §5.6.2 quantifies the trade-off: the RC source is not the ±250 ppm class — "For
  higher LFCLK accuracy (better than ±250 ppm), a low frequency crystal oscillator of
  32.768 kHz (LFXO) shall be used."
- The DTS still configures `&lfxo` (internal load capacitors, 17000 fF — the same value the
  DK ships) and `&hfxo` (internal, 15000 fF) (NCS: `cpuapp.dts:39-47`), but the defconfig's
  RC choice makes the LFXO node dormant unless an app switches the Kconfig.
- DC/DC enabled from reset: `&vregmain { regulator-initial-mode = <NRF5X_REG_MODE_DCDC>; }`
  (NCS: `cpuapp.dts:53-57`) — no LDO-vs-DCDC app work needed.
- GRTC: owned channels 0–11, child-owned 3,4,7–11 ("7-11 reserved for Zero Latency IRQs,
  3-4 for FLPR") — **identical to the DK** (NCS: `cpuapp.dts:59-66` vs the DK common dtsi).

### UART map and the console collision

- The cpuapp DTS binds all five console chosen nodes to `uart20`: `zephyr,console`,
  `shell-uart`, `uart-mcumgr`, `bt-mon-uart`, `bt-c2h-uart` (NCS:
  `ophelia4ev_nrf54l15_cpuapp.dts:16-20`). `&uart20` is the only UART enabled on the cpuapp
  target (`cpuapp.dts:68-70`); `uart30` carries pinctrl/speed in the shared dtsi but no
  `status = "okay"` there.
- **`uart20` pins are not the DK's.** Board pinctrl: TX `P1.04`, RX `P1.15` (pull-up),
  RTS `P1.06`, CTS `P1.07` (NCS: `ophelia4ev_nrf54l15-pinctrl.dtsi:7-28`). The DK's
  `uart20` is TX `P1.04` / RX `P1.05` (NCS:
  `nrf54l15dk_nrf54l_05_10_15-pinctrl.dtsi:14,22`). The Ophelia-IV RX lands on `P1.15`
  because that is where WE wired the TTL-232R-3V3 cable (EV §5.3.7, below) — a COBS overlay
  that assumed DK pins would put the RX line in the wrong place.
- Default `current-speed = <115200>` on both UARTs (NCS: `ophelia4ev_common.dtsi:90-102`);
  the repo's COBS overlays set 921600 on `uart20` (the `cobs-uart` chosen node), exactly as
  every carrier overlay already does. The board def has no `hw-flow-control`, matching the
  repo's 8N1 no-flow-control COBS convention; the RTS/CTS pins idle.
- Consequence for the CS apps (the raytac/minew pattern, `docs/hardware.md`): an
  `ophelia4ev` carrier conf must select the RTT console — `CONFIG_USE_SEGGER_RTT=y`,
  `CONFIG_RTT_CONSOLE=y`, `CONFIG_UART_CONSOLE=n` — because the board def routes the
  console onto the COBS UART itself, unlike the DK where console (`uart30`) and COBS
  (`uart20`) are separate. The `central.overlay`/reflector fragments bind only
  `cobs-uart = &uart20`; they do not touch the console nodes, so the carrier conf is what
  keeps log bytes off the COBS stream. `uart30` console pins (`P0.00`/`P0.01`) exist only
  on the unmounted P1 header (EV Table 7 pins 5/13), so the DK-style uart30 console is not
  physically reachable as shipped — RTT via the debug probe is the only console.

### Partition layout

- `ophelia4ev_nrf54l15_cpuapp.dts:156` includes the same vendor partition file as the DK
  (`dts/vendor/nordic/nrf54l15_cpuapp_partition.dtsi`): MCUboot 62 KB @ 0 (sized for
  FPROTECT), slot0 (image-0) **712 KB** @ 0x10000, slot1 712 KB @ 0xc2000, storage 36 KB @
  0x174000 (single-image build). When sysbuild adds the FLPR image
  (`WITH_FLPR_PARTITIONS`), slot0/slot1 shrink to 664 KB each and storage moves —
  irrelevant for the app-core-only CS builds.
- No pm_static file ships with the board def; the CS samples' single-image cpuapp builds
  land in slot0 exactly as on the DK. No partition mismatch found.

### Board defconfig

- cpuapp: `CONFIG_SERIAL=y`, `CONFIG_CONSOLE=y`, `CONFIG_UART_CONSOLE=y`, `CONFIG_GPIO=y`,
  `CONFIG_ARM_MPU=y`, `CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y` (NCS:
  `ophelia4ev_nrf54l15_cpuapp_defconfig`). `CONFIG_SERIAL=y` is already board-level —
  unlike the TAG, where it had to move into `initiator/prj.conf` (see `docs/hardware.md`).
- The only Kconfig default besides that is `HW_STACK_PROTECTION` (NCS:
  `Kconfig.defconfig:4-5`).

## 3. MiniEV connectors and power (EV-verified)

### CON4 — the one header that matters for the bench (fitted)

CON4 = WE 61031018221, 10-pin 2.54 mm, fitted as shipped (EV §5.9 BOM). Pinout (EV §5.3.7
Table 13 — matches the charting recon verbatim):

| CON4 | Function | Board connection | FTDI crossover (Figure 12) |
|---|---|---|---|
| 1–5 | GND | GND | GND (pin 5 shared with the cable's GND) |
| 6 | P1.06 | Module pad 17 (RTS) | CTS |
| 7 | 5V | External power supply connection (5 V) | VCC |
| 8 | P1.15 | Module pad D6 (RX) | TXD |
| 9 | P1.04 | Module pad C6 (TX) | RXD |
| 10 | P1.07 | Module pad 16 (CTS) | RTS |

- The FTDI crossover is EV-confirmed twice: §5.3.7's note ("CON4 pin header pins 5 to 10
  are used to connect the TTL-232R-3V3 FTDI cable") and the schematic (Figure 12), which
  labels the group TTL-232R-3V3 and draws the module's `P1.04/UTXD` → cable `RXD`,
  `P1.15/URXD` ← cable `TXD` crossover. The bench-read silkscreen string "TTL-232R-3V3
  (3.3 V Logic)" is a BENCH observation consistent with it (the literal "(3.3 V Logic)"
  suffix is not spelled out in the manual).
- Test pads duplicate the UART: TP32 = `P1.04`/UTXD, TP36 = `P1.15`/URXD, TP9 = `P1.06`/RTS,
  TP12 = `P1.07`/CTS, plus TP40 5 V, TP42 GND, TP14 LDO_OUT, TP13 VDD, TP20 VDD_MOD
  (EV Figure 12 test-pad column).

### CON2 — SWD, solder pads as shipped (fitted header must be soldered)

CON2 = 2×10, WE 61302021121 per Table 6 — but **"The mini evaluation board provides solder
pads for 2×10 pin connector"** (EV §5.6.3) and the BOM lists CON2 as n.m. — the 20-pin
header is **not mounted as shipped**; a header must be soldered (or the pads probed) before
any probe attaches. The charting rig statement "SWD via CON2, standard Segger 20-pin
layout" stands electrically; physically the pads are bare. Pinout (EV §5.3.5 Table 11):

| CON2 | Function | Board connection |
|---|---|---|
| 1 | VDD (probe VTref) | Module pad 8 VDD (VDD_MOD net, Figure 12) |
| 7 | SWDIO | Module pad 5 SWDIO |
| 9 | SWCLK | Module pad 4 SWCLK |
| 15 | /RESET | Module pad 6 RESET |
| 4,6,8,10,12,14,16,18,20 | GND | GND |
| 2,3,5,11,13,17,19 | — | unconnected |

One precision correction to the charting recon: GND is on **all even pins except pin 2** —
pin 2 is unconnected (standard Segger 20-pin layout keeps pin 2 NC). The recommended flash
adapter is "one of the 'Segger J-Link' family" (EV §5.6.3). The unmounted P1 header
duplicates SWD on pins 1–3 (SWDCLK/SWDIO//RESET, EV Table 7).

### CON3 — power jumpers (fitted), and the one destructive default

CON3 = 6-pin, WE 61030618221 (EV §5.3.6 Table 12): pin 1 = VDD (module side), pins 2–5 =
VDD_MOD (external 1.7–3.6 V input), pin 6 = LDO_OUT (regulator output). Factory jumpers
(EV §5.4.1 Table 14): **pin (1-2) set** (current-measure shunt) and **pin (5-6) set**
(5 V rail feeds the module through the onboard regulator). The manual's warning is
explicit, twice (EV §5.6.1.1 and §5.6.1.3): "The jumper on pin (5-6) of CON3 shall be
removed if the external power supply on CON3 (VDD_MOD: 1.7 V - 3.6 V) is used." Feeding
the module rail onto pins 2–5 while 5-6 is set ties an external rail to the regulator
output — never do it.

### Power options (EV §5.6.1 Table 16)

| # | Supply | Where | Regulator | CON3 |
|---|---|---|---|---|
| 1 | TTL-232R-3V3 cable | CON4 | active (jumper 5-6) | 1-2 set |
| 2 | External 5 V | CON4 pin 7 (5 V) + pins 1–5 (GND) | active (jumper 5-6) | 1-2 set |
| 3 | External 1.7–3.6 V | CON3 pins 2–5 (VDD_MOD) + CON4 pins 1–5 (GND) | inactive (**no jumper on 5-6**) | 1-2 set |

The regulator is a Würth MagI³C VDMM 171010550 (EV §5.9 BOM, IC2), which the manual
describes as regulating "the connected 5 V down to 3 V" (EV §5.6.1.1) — the bench measured
VDD_MOD ≈ 3.0 V (BENCH), inside the module's 1.7–3.6 V input range.

- **Level-match caveat (EV's own words, §5.4.2):** "When supplying the EV-Board with an
  FTDI cable via CON4, be aware that the voltage level of the UART lines of the FTDI cable
  may differ from the voltage level of the radio module." Consequence for the rig: any
  adapter wired to CON4 pin 8 (module RX, `P1.15`) must be 3.3 V logic. The module-level
  absolute maximum (VDD + 0.3 V on GPIO) is module-manual territory — the MiniEV manual
  does not state it; carried from the charting recon (BENCH) and the nRF54L15 module class.
  If module-level electrical limits are ever needed, that is UM_Ophelia-IV territory (EV
  reference [7]) — not verified in this pass.

### Reset button

S1 (WE 430152043826) drives /RESET, active low with a pull-up (EV §5.5.1). The only
user-accessible control on the board as shipped.

### P1/P2/P3 GPIO headers — all unmounted

P1 (1×13), P2 (1×4), P3 (1×11) are "additional assembly components" — **not fitted**
(EV §5.2 Table 6; §4: "By default, the mini evaluation board is assembled with the minimum
required pin headers to take the module into operation"). The full per-pin maps are EV
Tables 7–9; bench-relevant entries: P1 pin 5/13 = `P0.00`/`P0.01` (the `uart30` pins), P1
pin 12 = `P1.05` (the DK's `uart20` RX pin, unconnected on this carrier's fitted headers),
P3 pins 6/7 = XL1/XL2 (`P1.00`/`P1.01`).

### LFXO crystal option (EV §5.6.2)

Default: **no crystal** — Q1 (WE 830009706, 9 pF load, 3.2 × 1.6 mm), C9, C10 not
assembled; R1, R2 (0 Ω, Yageo RC0402FR-070RL) assembled, tying `P1.00/XL1`/`P1.01/XL2`
(module pads 12/13) to the GPIO nets. LFXO rework: fit Q1 + C9/C10 (13 pF worked on the
board; pad input capacitance 3 pF each, PCB parasitic 0.5–2 pF), remove R1/R2. Note: EV
§5.6.2 also says "Q1, C9, C10 shall be assembled and R3, R5 are not assembled" — that
sentence contradicts the same section's own R1/R2 instruction and the BOM (R3 = 10 Ω
assembled, no R5 exists); the BOM is the resolver.

## 4. Antenna (locked decision confirmed)

- **Default internal PCB antenna**: 22 pF 0402 capacitor on C2 (WE 885392005114) populated,
  C1 removed, C8/C11 unpopulated (EV §5.3.4.1, "smart antenna"; BOM agrees: C2 = 22 pF,
  C1/C8/C11/C12 n.m.). Charting's locked decision holds: `a1_4` presets only, both roles,
  RAS + IPT — `ophelia4ev_cent_a1_4`, `ophelia4ev_peri_a1_4` and the `_ipt` variants.
- External SMA path is rework: populate C1 (22 pF), remove C2, mount CON1 (SMA
  through-hole, WE 60312002114503, n.m. in the BOM); the qualified external antenna is
  the Würth Himalia
  2600130021 (EV §5.3.4). C8/C11 are optional fine-tuning positions.
- Single integrated PCB antenna, no RF switch → the overlay carries no
  `cs_antenna_switch` node and presets use `NUM_ANTENNAS=1` (`4_path_1_local.conf`), the
  same shape as Fanstel, ME54BE01, and ISP2454 (`docs/hardware.md`).
- Layout keep-outs: the assembly diagram marks "no metal" zones, 12.5 mm clearance around
  the antenna region (EV §5.8, Figure 13).

## 5. Module state: shipped DTM firmware

- **The module ships with DTM firmware.** EV §3 Table 3: order code 2621011022000 =
  "BYOF radio module with DTM (Direct Test Mode) firmware". No WE source names the exact
  build.
- **Bench identification (BENCH, charting 2026-09-08):** stock NCS DTM sample build,
  ~71 KB, console banner "Starting Direct Test Mode sample", no readback protection —
  RRAM reads freely through the standalone J-Link; flashing overwrites; `recover` only if
  a flash errors.
- **NCS cross-check (NCS):** the banner wording pins the build's NCS generation —
  NCS v3.0.0 through v3.3.0 print `Starting Direct Test Mode sample`
  (`samples/bluetooth/direct_test_mode/src/main.c:19`), v3.4.0 renamed the line to
  `Starting DTM sample`. The shipped image was therefore built from an NCS ≤ v3.3.0 (exact
  revision not stated anywhere). Also note the stock v3.4.0 DTM sample disables its UART
  console entirely (`CONFIG_CONSOLE=n`, `CONFIG_UART_CONSOLE=n`, `prj.conf:2-4`), so the
  console banner on the shipped image is a DTM-sample-derived build configuration, not the
  raw v3.4.0 sample.
- The upstream Zephyr board doc carries the standard Nordic readback-protection warning
  with `west flash --recover` as the remedy (NCS: `doc/index.rst:63-78`) — bench found no
  protection enabled, so it is boilerplate here, not a required step.

## 6. Bench recipes

Consolidated from the bench memories (each verified on this or a sibling rig; probe
serials for this bench: standalone J-Link **50124607**, DK counterpart **\*4404**).

### Probe discipline (two J-Links on the bus)

1. Never run `JLinkExe` without `-SelectEmuBySN <sn>` — with the standalone probe and the
   DK's onboard debugger both attached, an unforced attach picks an arbitrary probe and
   reads the wrong board (it does not fail or prompt). Always confirm the echoed `S/N:`
   line matches the intended probe before trusting a read.
2. Old-DLL quirks (system DLL 7.92m): `-Device CORTEX-M33 -If SWD -Speed 4000 -NoGui 1`
   works for the nRF54L15 (the `nRF54L15` device name is rejected); RTT is read via raw
   memory reads at `_SEGGER_RTT` (`JLinkRTTLogger`'s auto-search fails); a transient
   "Setting the debug port SELECT register failed" right after probe power-up clears on
   retry ~2 min later.
3. SWD wiring: CON2 pin 1 = VTref (VDD_MOD ≈ 3.0 V), 7 = SWDIO, 9 = SWCLK, 15 = /RESET,
   GND on even pins 4–20 (pin 2 NC). Solder the 2×10 header first (§3).

### Flash

1. Re-derive the probe identity first: `nrfutil device list` (ttyACM↔S/N mapping is
   unstable across reboots; re-derive, never assume).
2. Flash with the safe options — the tool defaults (`ERASE_ALL` + `RESET_NONE`) halt the
   nRF54L15 and risk the net core:

   ```
   nrfutil device program --firmware <zephyr.hex> --serial-number <SN> \
     --options chip_erase_mode=ERASE_RANGES_TOUCHED_BY_FIRMWARE,reset=RESET_SYSTEM,verify=VERIFY_READ
   ```

3. Boot caveat (nRF54L15 SecDom): `RESET_SYSTEM` may leave the app core halted; a
   `nrfutil device reset --serial-number <SN> --reset-kind RESET_HARD` boots it. Confirm
   boot via the RTT/RAM-dump recipe below (the board has no console VCOM of its own).
4. `recover` only on a flash error or a locked device — bench found readback protection
   off on the shipped module (§5).

### RTT console (the carrier's only console)

1. RTT control block at ELF symbol `_SEGGER_RTT`; read RAM there with `JLinkExe`
   (`mem`/`savebin`) — the 7.92m RTTLogger cannot find it by itself.
2. Stale-ring recipe (AN54LV-K15 precedent): the 1 KB up-buffer runs `NO_BLOCK_SKIP` and
   survives reflash; with the probe attached, corrupt the control-block ID
   (`w1 <_SEGGER_RTT addr>, 0xAA, 2` clobbering the `SE` of "SEGGER RTT"), then reset —
   the fresh boot re-inits the ring and its first ~1 KB is this boot's output only.
   Power-cycling the board works too.
3. The ophelia4ev carrier conf must select this RTT console (§2, UART map) — without it
   the board defconfig puts the console on the COBS UART.

### Serial identities and the COBS capture path

1. The FT232R adapter is a **clone** (`/dev/ttyUSB0`, USB serial `00000000`); address it
   via `/dev/serial/by-id` (`usb-FTDI_FT232R_USB_UART_*-if00`), and keep the DM-bench
   lesson on the radar: this adapter class has corrupted COBS streams with spurious
   `0x00` bytes when marginal — if events stop decoding while logs survive, suspect the
   adapter before the firmware.
2. Wire (charting rig): FT232 RX → CON4 pin 9 (module TX `P1.04`); FT232 TX not wired.
   Set 921600 8N1 no flow control (`stty -F /dev/ttyUSB0 921600 raw -echo`).
3. Capture with a count, never a duration — `--duration` starves its deadline under frame
   load; use `mars-acquisition --count N`.
4. Decode needs the Serializable envelope (COBS payload tags 0x00/0x01) and the current
   mars-bluetooth-hci crate (the repo's copy of the reader is stale otherwise).
5. **The module-TX → FT232-RX direction is the one unverified rig fact** — see the
   closing section.

### Power

1. Default bench path: 5 V into CON4 pin 7, GND on CON4 pins 1–5, CON3 jumpers stay
   factory (1-2 + 5-6); the MagI³C regulator feeds VDD_MOD ≈ 3.0 V (EV Table 16 row 2).
2. The same CON4 can carry a TTL-232R-3V3 cable doing power + UART together (EV row 1) —
   the rig's FT232 TX-only wiring leaves the power feed on pin 7 instead.
3. Never feed CON3 pins 2–5 while the 5-6 jumper is set (remove it first — EV §5.6.1.3).
4. Keep the adapter's logic at 3.3 V if CON4 pin 8 (`P1.15`, module RX) is ever wired
   (§3, level-match caveat).

## 7. Cross-check hooks for `ophelia4ev` bring-up

| # | Doc claim (source) | Bench check |
|---|---|---|
| 1 | `ophelia4ev/nrf54l15/cpuapp` exists in NCS v3.4.0 and WE maps the MiniEV onto it (AN §3; NCS `board.yml`) | Build one preset with `BOARD=ophelia4ev/nrf54l15/cpuapp` and confirm the board file resolves |
| 2 | `uart20` = TX `P1.04` / RX `P1.15` (NCS pinctrl; EV Table 13) | COBS overlay needs no pin override — but any DK pin assumption does not transfer |
| 3 | Board defconfig console on `uart20`; all five chosen nodes there (NCS dts) | Ship the RTT-console carrier conf; verify no log bytes pollute the COBS stream |
| 4 | 32 K source = RC by default; no crystal fitted (NCS defconfig; EV §5.6.2) | Do not copy the DK's XTAL default; log LFXO/RC choice per build; if CS timing looks off, this is suspect #1 |
| 5 | Partition layout identical to the DK (NCS: vendor dtsi) | Flash normally; slot0 712 KB |
| 6 | `mx25r64` node probes a floating SPI bus on the MiniEV (NCS dts; EV BOM) | Expect a boot-time JEDEC probe failure log; confirm it is non-fatal, or drop the node in the carrier overlay |
| 7 | LEDs/buttons/NFC phantom on the MiniEV (NCS dtsi; EV schematic) | Ignore unless an app references them |
| 8 | CON2 header unmounted (EV §5.6.3) | Solder a header before probe attach |
| 9 | Power options table (EV §5.6.1) | 5 V on CON4 pin 7, CON3 1-2 + 5-6 set; never CON3 VDD_MOD with 5-6 set |
| 10 | Internal antenna via C2 (EV §5.3.4.1) | No antenna rework for `a1_4`; loupe C2 if conducted RF is ever needed |
| 11 | Shipped image is a DTM-sample build from NCS ≤ v3.3.0 (EV §3; NCS banner history) | Readback open; plain `program` overwrites |
| 12 | Die revision not documented by WE | `nrfutil device info` / Programmer — log die revision in the bench ticket (the KAGA Engineering-B lesson) |

## Not found — explicitly / unverified

- **The capture path (module TX → FT232 RX) is unproven.** Charting wired it TX-only and
  it is the one rig fact no source can verify — only the module transmitting can. This is
  deliberate: the follow-up bench proof is ticket
  [#179](https://github.com/Metirionic/mars-cs-nrf54l/issues/179). The KAGA EVB precedent
  (its onboard FT232 bridge never delivered module-TX bytes on two units) is the cautionary
  tale for exactly this direction — check the adapter and the wiring first if capture is
  silent while ranging runs.
- **The exact NCS revision of the shipped DTM image**: narrowed to ≤ v3.3.0 by the banner
  wording (§5); no WE source states it.
- **Whether the CS controller needs XTAL-grade sleep-clock accuracy**: neither the MiniEV
  manual nor the NCS board def states a CS-specific LFCLK requirement, and this pass did
  not research the SDC's requirement — open question before any preset sets or keeps
  `CLOCK_CONTROL_NRF_K32SRC_RC`.
- **CON4 silkscreen literal "(3.3 V Logic)"**: bench-read string; the manual and schematic
  confirm only the TTL-232R-3V3 association (§3).
- **Module-level electrical limits** (GPIO absolute maximum VDD + 0.3 V, module RF specs):
  live in the module manual UM_Ophelia-IV (EV reference [7]) — not fetched for this doc;
  the charting-recon figure is carried as BENCH.
- **The "Ophelia-IV DK"/USB-port doc text** (NCS `doc/index.rst`): describes hardware
  (USB port, "Ophelia-IV EV") that no shipped product has — the WE catalog's only EV
  product is the MiniEV. Template boilerplate; no board fact depends on it.

## Sources

- Mini evaluation board user manual, Version 1.0, December 9, 2025 (37 pp.), fetched from
  https://www.we-online.com/components/products/manual/UM_Ophelia-IV_MiniEV_262111902xxxx%20(rev1.0).pdf
- ANR036 v1.1 (August 2025), "Build your own firmware — getting started with Zephyr",
  https://www.we-online.com/ANR036 (PDF: https://www.we-online.com/components/products/media/844929)
- Ophelia-IV product page (module 2621011022000 + EV-Kit 2621119022001):
  https://www.we-online.com/en/components/products/OPHELIA-IV
- Module manual reference (EV reference [7], not fetched):
  https://www.we-online.de/katalog/de/manual/2621011022000
- Himalia antenna: https://www.we-online.com/catalog/en/WIRL_ACCE_2600130021
- Nordic side (local NCS v3.4.0 Zephyr tree, `/home/aro/work/zephyr/`):
  `boards/we/ophelia4ev/` (all board-def facts, file names cited inline),
  `boards/nordic/nrf54l15dk/board.cmake` (runner inheritance),
  `boards/common/nrfutil.board.cmake` + `scripts/west_commands/runners/nrf_common.py`
  (runner defaults), `dts/vendor/nordic/nrf54l15_cpuapp_partition.dtsi` (partitions),
  `drivers/clock_control/Kconfig.nrf:35` (XTAL default),
  `samples/bluetooth/direct_test_mode/src/main.c:19` + tag history v3.0.0–v3.4.0 (banner).
- Charting recon 2026-09-08 (BENCH): rig, module state, probe serials, capture-path plan.