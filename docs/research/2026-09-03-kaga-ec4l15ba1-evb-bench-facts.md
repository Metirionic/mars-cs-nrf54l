# KAGA FEI EC4L15BA1-EVB: bench hardware facts

Research asset for [wayfinder #168](https://github.com/Metirionic/mars-cs-nrf54l/issues/168)
(map: [#167 KAGA EC4L15BA1 support charting](https://github.com/Metirionic/mars-cs-nrf54l/issues/167)).
Sources (all fetched 2026-09-03): KAGA FEI EC4L15BA1 Data Sheet V1.1 (2026-06-12, 37 pp.),
EC4L15BA1-EVB Evaluation Board / EVK manual Rev 1.0 (2026-01-26, 9 pp.), Firmware Writing
Manual Rev 1.0 (2026-02-06, 7 pp.), Bluetooth-module FAQ V1.2 (2026-09-01, 12 pp. — found via
the product page, not listed in the recon), KFEI BLE Overview V1.4 (2026-06-10), the
EC4L15BA1 product page, and — for Nordic-side facts only — the local NCS v3.4.0 Zephyr tree
(`/home/aro/work/zephyr/`). Every claim is tagged **DS** (module datasheet), **EVB**
(board manual), **FW** (firmware-writing manual), **FAQ** (module FAQ) or **OV** (overview);
Nordic-side claims are tagged **NCS** and are not passed off as KAGA facts.

## TL;DR

The single gating question — **what LFXO load capacitance should the `kaga_ec4l15ba1`
overlay set** — has **no primary-source answer, because there is no board-level LFXO
decision to make**: the 32.768 kHz crystal is **inside the module** (DS §5.1 block
diagram p.20, "X'tal 32.768kHz"; FAQ §2 Q2-4 p.8 "All EB- and EC-series modules
incorporate a 32.768 kHz crystal"), the 47-pin LGA exposes **no XL1/XL2 pads**
(DS §7 pin table pp.24–26), and **KAGA publishes no load-capacitance value anywhere**.
So the ISP2454 lesson (vendor prescribes 19 pF > the SoC's 18000 fF LFXO bank
BUILD_ASSERT) **does not transfer**: there is no vendor prescription to collide with the
assert. Overlay verdict: **set nothing** — ride the `nrf54l15dk` target default
`load-capacitance-femtofarad = <17000>` (NCS: `boards/nordic/nrf54l15dk/
nrf54l_05_10_15_cpuapp_common.dtsi:34-37`), which sits inside the asserted 3000–18000 fF
bank (NCS: `soc/nordic/nrf54l/soc.c:41-42,56-63`), and watch 32 kHz accuracy on bench.

The second headline: these modules **ship with APPROTECT enabled** (FW §1.2 p.5) and
**blank** (no firmware, FW/FAQ Table 2; EVB §2 p.3 "Blank Module") — the first flash on a
fresh module requires `nrfutil device recover` (FW §2.2 p.6), exactly the ISP2454 EVK
AP-protect playbook. And the board itself corrects the recon's rig picture in one
respect: **all pin headers except CN1 are not fitted** (EVB §5 note 2 p.5), while the
solder bridges SB1–SB5 and the CN2 power jumper are **factory-shorted traces** (EVB §4
schematic note p.4: "Please cut these 6 lines on the board if you want to separate U1
and U2") — so the FT232 COBS UART and USB power work out of the box, but any
jumper-wire rig on CN4/CN5/CN8/CN9 needs headers soldered first.

## 1. The 32.768 kHz crystal and the LFXO overlay value

- **The crystal is internal to the module.** DS §5.1 block diagram (p.20) draws
  "X'tal 32MHz" and "X'tal 32.768kHz" inside the module boundary next to the
  nRF54L15/nRF54L10/nRF54L05 QFN48. FAQ §2 Q2-4 (p.8): "All EB- and EC-series modules
  incorporate a 32.768 kHz crystal, so no external crystal is required." FAQ §2 Q2-3
  (p.7): all modules incorporate a 32 MHz crystal. OV (EC4L15 feature list): "Integrated
  System Clock / Sleep Clock, LC for DC/DC".
- **No crystal pads are exposed.** The DS §7 pin table (pp.24–26) lists all 47 pads:
  power/GND, GPIO (P0.00–P2.10), SWDIO/SWDCLK/nRESET, OUT_MOD/OUT_ANT RF pads —
  **no XL1/XL2**. The load network is not reachable from the carrier; the board overlay
  cannot add, change, or measure it.
- **No load-capacitance value is published.** Searched DS V1.1, EVB Rev 1.0, FW V1.0,
  FAQ V1.2, OV V1.4 and the product page: no CL in femto/picofarads, no crystal
  part number, no ppm tolerance for the 32 kHz crystal. The only 32 kHz accuracy
  statement is DS §4.1 note \*2 (p.18): "ANT specification requires +/-50ppm accuracy
  for 32.768kHz clock. The internal 32.768kHz crystal does not meet to +/-50ppm over
  the whole recommended operation temperature range." (i.e. ANT-cert builds must not
  rely on the internal crystal at temperature extremes — a certification caveat, not a
  load spec.)
- **Consequence for the overlay: no KAGA-prescribed value exists, so nothing to
  override.** The nRF54L15 LFXO bank is asserted to 3000–18000 fF with internal load
  capacitors (NCS: `soc/nordic/nrf54l/soc.c:41-42` `LFXO_CAP_MIN_FF`/`LFXO_CAP_MAX_FF`,
  BUILD_ASSERT at `soc.c:56-63`). The `nrf54l15dk/nrf54l15/cpuapp` target the carrier
  rides ships `&lfxo { load-capacitors = "internal"; load-capacitance-femtofarad =
  <17000>; }` (NCS: `boards/nordic/nrf54l15dk/nrf54l_05_10_15_cpuapp_common.dtsi:34-37`),
  inside the bank. **Decision: keep the DK default 17000 fF — no `&lfxo` override in the
  `kaga_ec4l15ba1` overlay.** 17000 fF configures the SoC-internal cap bank; the
  physical load the KAGA crystal actually sees is fixed by KAGA's internal matching
  network, which no overlay value changes. Bench watch item: if CS timing or any
  32 kHz-derived measurement drifts, measure the real LFXO offset (ppm) on bench; the
  definitive factory cap setting would have to come from KAGA FEI support
  (ml-module_contact@jp.kagafei.com, FAQ §4 Q4-6 p.12).

## 2. Flash flow and APPROTECT

- **Modules ship APPROTECT-enabled and blank.** FW §1.2 (p.5): "When initially shipped,
  APPROTECT is enabled" — with the initial state table (Disable = debug enabled /
  Enable = "data readout is disabled and debugging such as memory access is not
  possible"). FW §1.3 (p.5) lists the covered parts: EC4L15BA1, EC4L10BA1, EC4L05BA1,
  ES4L15BA1. The nRF54L Basic module itself ships with **no software** (FAQ §2 Q2-7
  p.8: "The nRF53- and nRF54L-series Basic modules are shipped without any software
  programmed"; FAQ Table 2 p.8: SoftDevice column = "Blank"; EVB §2 p.3: "Blank Module
  (formerly 'Basic Module') — a hardware module with no pre-installed firmware").
  (For contrast, the FAQ explains nRF52-series modules ship preprogrammed with
  SoftDevice behind AP PROTECT — that story does not apply to EC4L.)
- **Vendor flash flow (FW §2, pp.6–7):** install the **latest nRF Util**, VS Code, and
  the nRF Connect SDK. Because the module ships APPROTECT-enabled, "the firmware cannot
  be written as it is" — first run **`nrfutil device recover`**, which "will disable the
  APPROTECT feature and allow you to continue writing firmware", and the disable is
  retained after reset. FW §2.2 note (p.6): recover "erases the flash memory and then
  writes a firmware into the recovered flash memory. This firmware prevents the
  readback protection from enabling itself again after a pin reset or power cycle",
  while a plain erase command re-arms APPROTECT (setting area erased to 0xFF →
  enabled). In-firmware control: `CONFIG_NRF_APPROTECT_LOCK=y` to enable, and
  `CONFIG_NRF_APPROTECT_DISABLE=y` to disable (FW §3.1–3.2, p.7). FW points to Nordic's
  Product Spec §9.2 "Access port protection" for details.
  → Same playbook as the ISP2454 EVK bench lesson: **recover first, then flash; expect
  erase to re-arm the lock.**
- **Debug probes the vendor names:** EVB §9 (p.7) shows the SWD chain with a
  **Nordic-DK** as the attached debugger, "nRF Connect etc." host software, and
  "[JTAG DEBUG TOOLS] J-Link Lite CortexM-9 etc."; the SWD interface is specified as
  "10-pin 1.27mm pitch connector" with SWDIO/SWCLK. DS §5.2 (p.21): "Application
  software can be programmed via a debug probe such as the SEGGER J-Link. For J-Link
  connectivity, it is recommended to populate the target board with a 10-pin, 1.27mm
  pitch dual-row connector (e.g., PSS-720153-05 by Hirosugi Instrument)." The EVK adds
  a **J-Link Lite** to the box (EVB §3 p.4; FAQ §3 Q3-2 p.11), restricted to use with
  the board it shipped with (EVB §3 note \*1 p.4). FAQ §3 Q3-5 (p.11): evaluation
  boards with Basic modules "require a J-Link Lite to program application software".
- **No onboard debugger on the EVB**: CN6 mini-USB carries only the FT232RNQ
  USB-UART bridge plus 5 V power (EVB §5 p.5, §8 p.6, §10 item 3 p.8) — flash + RTT
  must come from an external probe (DK DEBUG OUT per the recon, or the EVK's J-Link
  Lite) over CN1/CN8.
- **nrfutil/J-Link version statements: none found.** No minimum nRF Util version
  ("latest" only), no J-Link software/firmware version, no compatibility matrix in any
  source. Nothing contradicts the repo's bench tooling; the workstation's old-system-
  DLL caveats (J-Link 7.92m, `nrfutil device` as primary path) remain our own bench
  memory, not doc facts.

## 3. SWD paths: CN1 (mounted) and CN8 (header not fitted)

- **CN1 = PSS-720153-05, 10-pin 1.27 mm dual-row, the programming connector**
  (EVB §4 schematic p.4, labeled "CN1: SWD (for J-link lite)" in §5 p.5). It is the
  one connector **not** in the not-mounted list (EVB §5 note 2 p.5), i.e. fitted.
- **CN1 is not the standard ARM 10-pin SWD pinout.** Full per-pin map, read from a
  400 DPI render of the EVB §4 schematic (p.4) during rig verification (#170) —
  the bench's adapted DK-DEBUG-OUT cable matches it, and CN1 pin 1 (square pad) is
  keyed:

  | CN1 pin | Net | Module pad |
  |---|---|---|
  | 1 | VDD | rail |
  | 2 | SWDIO | 14 |
  | 3 | GND | — |
  | 4 | SWDCLK | 15 |
  | 5 | GND | — |
  | 6 | NC | — |
  | 7 | NC (no stub drawn) | — |
  | 8 | NC | — |
  | 9 | GND | — |
  | 10 | NC | — |

  The silkscreen confirms the four named nets (`VDD GND SWCL SWIO`). **VDD is
  present (pin 1)** — an earlier pass of this research wrongly reported "no VTref";
  a probe's voltage reference can be fed from CN1 pin 1. nRESET is genuinely absent
  from CN1: module pad 20 (DS §7 p.25) is reachable only via the SW1 push button
  (active low, EVB §5 note 6 p.5) and CN5 pin 9 (N.M. header). Practical
  consequence: a straight-through 1.27 mm ribbon from a standard ARM-10 debug
  header (e.g. the DK DEBUG OUT) **will not map 1:1** — adapt pin-by-pin against
  the table above (VDD/VTref→1, SWDIO→2, GND→3/5/9, SWDCLK→4), leave nRESET
  unmapped, and reset via SW1.
- **CN8 (2.54 mm, 10-pin) breaks out the same SWD plus UART and power** — pins:
  1 VDD, 2 GND, 3 SWCL, 4 SWIO, 5 P1.06, 6 P1.07, 7 P1.04, 8 P1.05, 9 P0.00, 10 P0.03
  (EVB §7 table p.6). **The header is not fitted** (EVB §5 note 2 p.5), so the
  jumper-wire alternative to CN1 needs a header soldered first. Same for CN4 (VDD,
  GND, P1.09, P1.10, P0.02, P1.03, P1.02, P2.00–P2.02), CN5 (P2.03–P2.10, NRST,
  P0.04) and CN9 (P0.01, P1.08, P1.14, P1.13, P1.12, P1.11) — all N.M.
- Module-side debug pins: SWDIO = pad 14 ("bidirectional with standard drive and
  on-chip pull-up"), SWDCLK = pad 15 ("input with on-chip pull-down"), nRESET = pad 20
  (DS §7 pp.24–25) — single debug port, one die; both cores flash through it as on the
  DK.

## 4. FT232 UART, power defaults, and the solder bridges

- **Bridge-to-pin map** (EVB §4 schematic p.4; module pad numbers from DS §7 p.24):

  | Bridge | FT232RNQ side | Module side | Module pad |
  |---|---|---|---|
  | SB1 | TXD (30) | RX | 9 = P1.05 |
  | SB2 | RXD (2) | TX | 10 = P1.04 |
  | SB3 | RTS (32) | CTS | 11 = P1.07 |
  | SB4 | CTS (8) | RTS | 12 = P1.06 |
  | SB5 | VCCIO (1) | VDD rail | — |
  | CN2 | 3V3OUT (16) | VDD | — |

  The UART pin assignment is also stated in prose (EVB §10 item 3 p.8): P1.04 = TX,
  P1.05 = RX, P1.06 = RTS, P1.07 = CTS — **same uart20/P1.04/P1.05 COBS pins as the
  DK and the ISP2454 EVK**, so the COBS transport ports over unchanged. No baud rate
  is stated anywhere in the KAGA docs.
- **All six bridges are factory-shorted traces, not open pads.** EVB §4 schematic note
  (p.4): "Solder Bridges — Please cut these 6 lines on the board if you want to
  separate U1 and U2", with one dashed cut line running through CN2 + SB1–SB5.
  SB5 ties the FT232's VCCIO to VDD, so **UART IO levels track the module rail**
  (3.3 V by default). Consequence: **the COBS UART over CN6 works out of the box** —
  no soldering; cutting is only for fully separating the FT232 from the module.
- **Power default:** "By default, the module is powered with 3.3V supplied from the
  FT232RQ's 3V3OUT pin" (EVB §8 p.6) — through the CN2 short (layout label: "CN2:
  Jumper of internal 3V3 — Default: Short", §5 p.5; schematic: 3V3OUT pin 16 → CN2
  pins 1-2 → VDD). For an external supply (1.7–3.6 V on CN4 or CN8 VDD/GND), "cut
  pins 1 and 2 of CN2" (EVB §10 item 2 p.8; repeated on the schematic).
- **Reconciling "Default: Short" with "N.M."**: EVB §5 note 2 (p.5) lists
  "CN2,3,4,5,7,8,9, C2,3, FB1, SB1-5, JP1 are not mounted (N.M.)" while the §5 layout
  labels CN2 and CN3 "Default: Short" and §8 says USB power works by default. Reading
  them together: the **electrical default is short** (factory trace/pad-bridge), the
  **N.M. means no connector hardware** (no header pins, no jumper cap) is fitted — so
  "cutting" CN2/CN3 is trace surgery or solder-in-a-header work, not moving a cap.
  Bench smoke check: D1 (mounted 3.3 V indicator LED) lights when USB is plugged →
  default USB power path intact; D2 (UART TX) / D3 (UART RX) flicker on COBS traffic
  (they hang off FT232 TXLED/RXLED pins 21/22 via 220 Ω, EVB §4 p.4).
- **CN3 = VDD current monitor** (layout: "Default: Short"): cut the trace between pins
  1 and 2 and put the ammeter across (EVB §10 item 1 p.8). C2/C3 (1608, N.M.) are
  spare VDD decoupling pads.
- **Rail voltages**: VDD operating range 1.7–3.6 V, extended-temp 1.7–3.4 V, POR
  1.75 V (DS §4.1 p.18); absolute max +3.9 V (DS §3 p.17); DC specs quoted at
  VDD = 3.0 V (DS §4.2 p.18); VIO max = VDD + 0.3 V (DS §3 p.17). The EVB's default
  3.3 V VDD with SB5 tying VCCIO to VDD is rail-matched FT232↔module — no
  1.8 V/3.0 V mixed-level caveat like the ISP2454 rig.
- **USB front end**: CN6 mini-USB → VBUS (ESD array + C1 4.7 µF; ferrite FB1 N.M.) →
  FT232RNQ; FTDI D2XX/VCOM drivers from ftdichip.com (EVB §4 p.4, §10 item 3 p.8).

## 5. Antenna selection (JP1 / CN7 / pads 22-23)

- JP1 = "Antenna selector — Default: Internal Antenna", N.M. as hardware; CN7 =
  U.FL-R-SMT RF connector, N.M. (EVB §5 p.5). The EVB routes OUT_MOD (pad 22) to
  either the internal antenna path (ANT-C) or the U.FL (EXT-C) via JP1.
- DS §5.2 reference circuit (p.21): for internal-antenna use "connect PAD22 and PAD23
  as short as possible"; for antenna/RF-conduction measurement, "draw a tie line of
  PAD22 and PAD23 outside a module". DS §7 (p.25): pad 22 OUT_MOD "should be connected
  to Pin 23 OUT_ANT for normal operation" (pad 23 = internal antenna).
- Net effect unchanged from the recon: exactly one integrated PCB antenna, no RF
  switch part; conducted RF only via the unpopulated CN7 + JP1 rework.

## 6. Silicon/die revision expectations

- **No die-revision statement exists in any KAGA source** (DS V1.1, EVB Rev 1.0,
  FW V1.0, FAQ V1.2, OV V1.4, product page). There is **no Engineering-B warning
  analog** to the Insight SiP shop page. Product-page status is "Mass Production";
  the user code is **nRF54L15-QFAA-R** (DS §2.2 item 1 p.4 — the standard production
  package code), marking = part number, lot number, radio-law IDs (MIC/FCC/ISED) and
  manufacturer on the shield (DS §2.2 item 7 p.4).
- **ES4L15BA1 is a real, separate part** — not a typo for EC4L: it has its own product
  page (OV p.3: https://www.kagafei.com/jp/eng/products/wireless-modules/bluetooth/ES4L15BA1.html),
  is covered by the same FW manual (FW §1.3 p.5), and differs electrically: output
  power range **−46 to +8 dBm vs −46 to +7 dBm for EC4L** (FAQ §2 Q2-11 p.9; DS
  general-items note 4 p.16 separately requires TX ≤ +7 dBm for radio regulations),
  and **ES-series modules have no internal 32.768 kHz crystal** — "customers can choose
  either to use an external 32.768 kHz crystal or to use the RC oscillator" (FAQ §2
  Q2-4 p.8). Bench consequence: **read the laser marking on the shield before bench
  work** — an ES4L15BA1 on the bench changes the 32 kHz story (no internal crystal →
  the LFXO overlay question re-opens) and the TX-power ceiling; the EC4L15BA1-EVB
  manual only ever shows the EC4L15BA1 mounted.
- Die revision itself (ISP2454-style Eng-B check): not documented by KAGA — read it on
  bench via `nrfutil device info` / Programmer and log it (same cross-check hook as
  the ISP2454 doc).

## 7. DC/DC vs LDO (module-level fact that gates the app config)

- **The nRF54L module supports DC/DC mode only — LDO mode is not supported.** DS §5.1
  block-diagram note (p.20): "\*1 LDO-only mode: Not supported" on the integrated
  "DC/DC LC Filter(DCC)" (the LC is inside the module — OV EC4L15 feature list
  "Integrated … LC for DC/DC"; FAQ §2 Q2-6 p.8 points to the OV comparison table).
  FAQ §3 Q3-3 (p.11): "nRF54L-series modules support only DC/DC converter mode and do
  not support LDO mode" — and notes NCS samples assume DC/DC, so for EC4L15 the sample
  default just works (the LDO-mode reconfiguration the FAQ prescribes for other
  modules is unnecessary here; FAQ Q3-4 confirms the inductor is pre-mounted on EVBs
  that need one).

## 8. Cross-check hooks for `kaga_ec4l15ba1` bring-up

| # | Doc claim (source) | Bench check |
|---|---|---|
| 1 | 32.768 kHz crystal internal to module, no CL published (DS §5.1 p.20; §7 pp.24–26; FAQ Q2-4 p.8) | No `&lfxo` override in the overlay (DK default 17000 fF stays); read die/module marking; if 32 kHz accuracy matters, measure LFXO ppm |
| 2 | Ships APPROTECT-enabled + blank (FW §1.2 p.5; EVB §2 p.3) | Fresh module: `nrfutil device recover` before first flash; expect erase to re-arm the lock |
| 3 | CN1 = 10-pin 1.27 mm SWD, non-ARM pinout: VDD=1, SWDIO=2, GND=3/5/9, SWDCLK=4, 6/7/8/10 NC, no nRESET (EVB §4 p.4, resolved at 400 DPI) | Bench check DONE (#170): the adapted DK-DEBUG-OUT cable matches and the SWD path works; reset via SW1 |
| 4 | Only CN1 + CN6 fitted; CN4/5/7/8/9 headers N.M. (EVB §5 note 2 p.5) | Solder a header on CN8 before any jumper-wire SWD/UART rig |
| 5 | SB1–SB5 + CN2 factory-shorted (EVB §4 p.4 note) | USB plug → D1 lights (3.3 V rail); COBS smoke test on CN6 at our chosen baud (P1.04 TX / P1.05 RX) |
| 6 | SB5 ties FT232 VCCIO = VDD (EVB §4 p.4) | Confirm UART IO level equals measured VDD before wiring any external adapter |
| 7 | JP1 default internal antenna; CN7 U.FL N.M. (EVB §5 p.5; DS §5.2 p.21) | Loupe JP1/CN7 if conducted RF is ever needed |
| 8 | Module may be EC4L15BA1 or (mis)marked ES4L15BA1 (FAQ Q2-4/Q2-11 pp.8–9) | Read shield marking; if ES4L15BA1: no internal 32 kHz crystal, +8 dBm cap |
| 9 | DC/DC-only, LDO unsupported (DS §5.1 p.20; FAQ Q3-3 p.11) | Keep the NCS-sample DC/DC defaults; no LDO Kconfig work |
| 10 | Die revision not documented | `nrfutil device info` / Programmer → log die revision in the bench ticket |

## Not found — explicitly

- **LFXO load-capacitance value** for the internal 32.768 kHz crystal: absent from
  every KAGA source (datasheet, EVB manual, FW manual, FAQ, overview, product page).
  The crystal has no exposed pads, so the question is closed by construction; the
  factory cap setting would need to come from KAGA FEI support.
- **Die revision / engineering-sample marking guidance**: no statement in any source;
  no Engineering-B analog to Insight's shop-page warning. Bench-read required.
- **nrfutil/J-Link version or compatibility statements**: none (only "latest nRF
  Util", "J-Link Lite CortexM-9 etc.", "a debug probe such as the SEGGER J-Link").
- **UART baud rate** for the FT232 path: no baud stated anywhere (921600 COBS is our
  own rig choice).
- ~~**Complete CN1 per-pin SWD map**~~ — **RESOLVED during rig verification (#170)**:
  a 400 DPI render of the EVB §4 schematic is fully legible; the complete map
  (VDD=1, SWDIO=2, GND=3/5/9, SWDCLK=4, 6/7/8/10 NC) is recorded in §3 above.
  Remaining open item from the original listing: only the "VDD present on CN1"
  correction stands (the initial raster pass misread it as "no VTref").
- **Quick Start Guide** (referenced by FAQ Q3-3/Q4-6 as the exact flashing/dev-env
  walkthrough): distributed only via the restricted-access site printed on the
  information card in the box, or from KAGA FEI sales
  (ml-module_contact@jp.kagafei.com). Not publicly downloadable.
- **Box contents beyond EVB + USB cable / EVK + J-Link Lite + USB cable(s)**: EVB §3
  (p.4) lists exactly those; FAQ Q3-2 adds an "information card". No further
  itemization.

## Sources

- Data Sheet V1.1 (2026-06-12):
  https://www.kagafei.com/jp/products/wireless-modules/bluetooth/File/__icsFiles/afieldfile/2026/06/29/EC4L15BA1_EC4L10BA1_EC4L05BA1_DataSheet_V1_1_20260612E.pdf
- Evaluation Board / Kit manual Rev 1.0 (2026-01-26):
  https://www.kagafei.com/jp/products/wireless-modules/bluetooth/File/__icsFiles/afieldfile/2026/02/12/EC4L15BA1_EVBManual_V1_0_20260126.pdf
- Firmware Writing Manual Rev 1.0 (2026-02-06):
  https://www.kagafei.com/jp/products/wireless-modules/bluetooth/File/__icsFiles/afieldfile/2026/02/12/FirmwareWritingManual_V1_0_54module_20260206E.pdf
- Bluetooth-module FAQ V1.2 (2026-09-01, linked from the product page):
  https://www.kagafei.com/jp/products/wireless-modules/bluetooth/File/__icsFiles/afieldfile/2026/09/02/FAQ_BLE_V1_2E_20260901.pdf
- KFEI BLE Overview V1.4 (2026-06-10):
  https://www.kagafei.com/jp/products/wireless-modules/bluetooth/File/__icsFiles/afieldfile/2026/06/15/KFEI_BLE_Overview_V1_4JE_20260610.pdf
- Product page: https://www.kagafei.com/jp/eng/products/wireless-modules/bluetooth/EC4L15BA1.html
- ES4L15BA1 product page: https://www.kagafei.com/jp/eng/products/wireless-modules/bluetooth/ES4L15BA1.html
- Nordic side (local NCS v3.4.0 Zephyr tree): `soc/nordic/nrf54l/soc.c:41-63`
  (LFXO bank assert), `boards/nordic/nrf54l15dk/nrf54l_05_10_15_cpuapp_common.dtsi:34-37`
  (DK LFXO default 17000 fF).
