# Insight SiP ISP2454 EVK (ISP2454-LX-EB): bench hardware facts

Research asset for [wayfinder #157](https://github.com/Metirionic/mars-cs-nrf54l/issues/157)
(map: [#156 ISP2454 support charting](https://github.com/Metirionic/mars-cs-nrf54l/issues/156)).
Sources: Insight SiP ISP2454 DK Data Sheet (`isp_ble_DS2454_DK.pdf`, R0),
ISP2454 Data Sheet R6 (09/06/2026), Application note AN250502 R0 ("Use of the ISP2454-LX
Development Kit"), ISP2454 Test Board schematic (`ISP2454_LX_TB_Schematic_RevB.pdf`, Rev 2,
2025-12-05), the ISP2454 product page and webshop, and — for Nordic-side facts only — the
local NCS v3.4.0 Zephyr tree (`/home/aro/work/zephyr/boards/nordic/nrf54l15dk/`). Every
claim is tagged EVK (ISP-specific doc) or DK (Nordic side); DK facts are not passed off as
EVK facts. Kit PDFs fetched 2026-08-31.

## TL;DR

The EVK is the **ISP2454-LX-EB Evaluation Board (85 €)**: an **ISP130603 "Interface Board"**
with an integrated **J-Link OB JTAG/SWD emulator** plus a plug-on **ISP2454-LX-TB Test Board**
(47 x 42 mm) carrying the **ISP2454-LX** module (nRF54L15, 1524 KB RRAM, 256 KB RAM,
integrated antenna; 32 kHz crystal; no 4 MB flash). The COBS transport ports over trivially:
the EVK docs themselves say the default log UART is **uart20, TX P1.04 / RX P1.05**, and
prescribe exactly our rig — an **external USB-UART adapter on Interface-Board header pins
P36 (P1.04/TX) and P37 (P1.05/RX)**. Caveats: the EVK runs the module at a **3.0 V rail**
(not the DK's 1.8 V — an FT232 at 3.3 V sits exactly at the module's VCC+0.3 V absolute-max
boundary), the TB's 3V3–VCC_nRF jumper (J1) is the only supply isolation (FT232 VCC must
stay unconnected — Raytac backfeed lesson applies), and the EVK shop page warns the EBs
carry an **nRF54L15 Engineering B** die. Antenna story for a1_4: exactly one internal PCB
antenna, no switch, no u.FL variant — an SMA on the TB (via 0 Ω links) is the only
conducted-RF access.

## 1. Exact kit name / part number

- Order code: **ISP2454-LX-EB**, marketed as "ISP2454 DK" (one-pager title) and
  "ISP2454-LX Development Kit" (AN250502 title) [DS2454_DK, p.1;
  AN250502 p.1]. https://www.insightsip.com/fichiers_insightsip/pdf/ble/ISP2454/isp_ble_DS2454_DK.pdf
- Ordering scheme per module datasheet (§6.6, p.21): `ISP2454-TT-ZZ` with TT ∈ {LL, LX, LP}
  and ZZ ∈ {EB = Evaluation Board, TB = Test Board, ST/JT/RS = bare-module packaging}.
  The webshop lists exactly three ISP2454 items: ISP2454-LX, **ISP2454-LX-EB**,
  **ISP2454-LX-TB** (so only LX kits are actually sold) [DS2454 §6.6;
  https://www.insightsip.com/shop-online/product/listing/33-isp2454].
- Box contents (EB): **one ISP130603 Interface Board with integrated J-Link OB JTAG/SWD
  emulator + one ISP2454-LX-TB Test Board** with the ISP2454 module mounted
  [DS2454_DK p.1; AN250502 §2.1 p.3; DS2454 §5.1 p.17]. The shop page says "delivered in a
  box with boards and cables as listed in the datasheet" (85,00 €, ex-stock) — the cable
  count is not itemized in any public doc (unconfirmed: check the box/invoice).
  https://www.insightsip.com/shop-online/product/100-isp2454-lx-eb
- Silkscreen: Test Board silk reads **"ISP2454-LX_TB"** (rev letter; schematic title block:
  "ISP2454-LX_TB_B", Revision 2, 2025-12-05) [AN250502 §2.4 p.6 photo; TB schematic RevB
  title block]. The Interface Board silk reads **"ISP27001A / ISP27001B Interface Board"**
  while all docs call the board **ISP130603** (revs G and H both appear in AN photos:
  overview "ISP130603G", UART-wiring caption "ISP2454LX TB on ISP130603H")
  [AN250502 §2.3 p.4, §5 p.18, §6 p.21].
- Kit pictures: DS2454_DK p.1 (Interface Board + Test Board composite) and AN250502 §2.1
  p.3 (stacked assembled setup with USB cable).
- Shop warning on the EB listing: **"Evaluation Boards based on nRF54L15 Engineering B
  version"** (secondary — vendor shop page, but first-party) → the module's die is Eng-B.

## 2. Onboard debugger / J-Link

- The ISP130603 Interface Board integrates a **J-Link OB JTAG/SWD emulator**
  [DS2454_DK p.1; AN250502 §2.1 p.3]. Its host MCU is Atmel-branded in the AN photo; the
  exact part number is not published (unconfirmed — read the chip marking on bench).
  [AN250502 §2.3 p.4 photo]
- Target access: the ISP2454 module exposes **SWDIO (pin 28), SWDCLK (pin 30), nRESET
  (pin 13)** of its single nRF54L15 die [DS2454 §3 p.12–13]. There is one chip / one SWD
  port; the app-core and net-core images live in the same 1524 KB RRAM [DS2454 §1 p.4 and
  block diagram p.5], so flashing through this SWD covers both — same single-debug-port
  model as the nRF54L15DK. The AN's flow is the standard Nordic one: build for board target
  **`nrf54l15dk/nrf54l15/cpuapp`**, then **Recover Board → Flash** in VS Code / nRF Connect
  [AN250502 §3.2.4/§4 pp.11,15; §5 p.19; §6 p.22].
- Physical debug paths (EVK-fact, TB schematic RevB): J3 = 2x5 header (Samtec FTSH-101-L-DV
  pinout: VTref=VCC_nRF, SWDIO, GND, SWCLK, GND, SWO/TDO, nRESET) — the AN describes it as
  "2x5 pin header for programming and debug when connected to a Nordic Development Board";
  J12 = "Conn_Jlink_10pin" second 10-pin SWD header; plus the 2x20 stacking connectors.
  The AN also lists a "JTAG footprint for programming and debug when connected to a Segger
  J-Link" on the TB [AN250502 §2.4 p.6 — i.e. an external J-Link probe works instead of the OB].
- RTT: not explicitly claimed for the OB in any Insight doc. The AN recommends installing
  the **latest J-Link software** (§3.2.1 p.9, screenshot shows V8.16), uses
  `CONFIG_LOG_BACKEND_RTT` / SEGGER RTT in its Kconfig examples and notes "RTT and UART can
  run in parallel on the nRF54L15" [AN250502 §3.1 p.8, §5 p.18]. J-Link OB emulators
  generally support RTT — treat as likely (unconfirmed: run JLinkExe/nrfutil RTT against
  the EVK once).
- Driver-version caveat (bench-relevant): the workstation's system J-Link DLL is V7.92m
  with no nRF54L15 device entry, and the `-Device CORTEX-M33` workaround has been the
  standing mitigation on other nRF54L15 J-Link probes (repo bench memory,
  `tag-debug-out-flash-quirks` / `cobs-decode-envelope-jlink-quirks`). The EVK's OB is used
  through the *same host-side J-Link DLL* by nrfutil/JLinkExe, so the caveat applies
  unchanged — either upgrade to the latest J-Link pack (what the AN prescribes) or reuse
  `-Device CORTEX-M33`. Exact minimum J-Link version for nRF54L15 support unconfirmed.

## 3. FT232 wiring to uart20 (P1.04 / P1.05)

- **Explicitly supported, and it is the documented UART path.** AN250502 states twice
  (§5 p.18, §6 p.21): "Initially, the UART uses to log is the UART20 which uses the
  peripheral P1. TX is on the P1.04 GPIO pin and RX is on P1.05 GPIO pin", and prescribes:
  "Connect TX on pin **P36** on the interface board, Connect RX to pin **P37**", with a
  photo of an external USB-UART adapter wired to P36/P37. The annex pinctrl screenshot even
  shows `NRF_PSEL(UART_TX, 1, 4)` in the uart20 pinctrl block [AN250502 p.18, p.23].
- The AN's §2.6 pin-mapping table (p.7) confirms the cross-board mapping: Interface-Board
  pin **P36 ↔ ISP2454 P1.04**, pin **P37 ↔ P1.05** (module pads 40 and 38 respectively;
  both pins exist on all module variants [DS2454 §3 p.13]). The TB's two 2x20 connectors
  (J8/J9) carry them to the Interface Board [TB schematic, IB CONNECTORS sheet].
- So for COBS: adapter RX ← IB silk P36 (module TX), adapter TX (optional, commands only)
  → IB silk P37. AN example baud is 115200; 921600 8N1 is a firmware-side setting (project
  convention), nothing EVK-side limits it beyond trace quality (unconfirmed on the IB header).
- On-board USB-UART bridge: the only USB connector on the Interface Board feeds the J-Link
  OB [AN250502 §2.3 p.4]. The IB has **"UART & SWD enable jumpers"** ("connect or
  disconnect Debug and UART lines from the test board") [AN250502 §2.3 p.5], but the AN
  never uses an OB VCOM for UART logs — it always prescribes the external adapter. Whether
  the OB's VCOM (if it exists) lands on P1.04/P1.05 or any module UART is **unconfirmed —
  verify on bench** (plug the IB USB, check for a CDC interface next to the J-Link enum,
  continuity-check the middle jumper block). Do not assume the OB VCOM can carry COBS.
- Levels: the 1.8 V figure applies to the *Nordic DK* (DK fact), not this EVK. The module's
  operating supply is **1.7–3.6 V** [DS2454 §2.3 p.7], and the EVK's default 3 V source is
  the embedded regulator (see §4), with power tables quoted at 3.0/3.1 V [DS2454 §2.4 p.7].
  So the default header IO level is **3.0 V nominal**. Absolute-max IO voltage is VCC+0.3 V
  for VCC ≤ 3.5 V [DS2454 §2.2 p.6] — a 3.3 V-driven FT232 sits exactly at that boundary
  against a 3.0 V rail. Options (bench decision, no EVK doc prescribes one): tie the
  FT232-side IO to the board's own 3.0 V rail (VCCIO config), or move IB J4 to EXT and feed
  1.8 V (1.7–3.6 V allowed) to mimic DK levels — in that mode the debugger is unpowered
  [AN250502 §2.3 p.5].
- uart30: the DK-default pins P0.00/P0.01 are **not mapped through to the Interface Board**
  (nothing in the AN §2.6 table maps them; they exist only on the TB's test connectors
  J5/J6 [TB schematic, TEST CONNECTORS sheet]). Insight's own uart30 example remaps
  pinctrl to **TX P0.02 / RX P0.03 @ 19200** and wires those IB header positions with a
  photo [AN250502 Annex p.23]. Any uart30 console on this EVK must follow Insight's remap,
  not the DK pinctrl. (The project's console path on this rig is RTT, so this only matters
  as an alternative.)

## 4. Power path

Selector/jumper matrix from AN250502 §2.3 (p.4–5) and the TB schematic; default positions
below are read from the AN photos (unconfirmed — eyeball on bench):

| Jumper | On | Function | Default (per AN photos) |
|---|---|---|---|
| **J2** | IB | 5 V source: **USB** vs **EXT_Supply (max 5.5 V)** | USB |
| **J4** | IB | 3 V source: **REG** (embedded regulator from 5 V) / **BATT** (CR2032, holder on IB underside) / **EXT** (external 1.7–3.6 V) | REG |
| Current-meas. jumpers (silk J3/J5 area) | IB | insert ammeter in 5 V rail and in 3 V rail | fitted (shunted) |
| **SWD & UART enable jumpers** | IB | connect/disconnect debug and UART lines from the TB | fitted (connected) |
| **J1** (SMT-102-01-L-SH-A) | TB | 3V3 → **VCC_nRF** (module pin 26) supply feed / isolation | fitted |

- Debugger power: "with such supply sources [CR2032 battery or external 3V] the debugger
  will not be supplied" [AN250502 §2.3 p.5] — the OB is fed from the USB/REG path; on
  J4=BATT/EXT expect no flashing (bench-verify).
- Backfeed exposure: the 2x20s and IB GPIO headers carry supply pins ("All GPIOs & Supply
  pins", "3V3/GND" on J8/J9) with **no** series isolation; the only isolation points are the
  TB J1 (module VCC) and the IB SWD&UART line jumpers. An FT232 with VCC tied to a 3V3
  header pin will therefore backfeed the module rail (and the IB 3 V rail) exactly as on the
  Raytac jig. Mitigation for the rig: leave adapter VCC unconnected (GND+TX/RX only); remove
  TB J1 whenever the board is powered from an external source, and open the IB SWD/UART
  jumpers whenever the debugger is unpowered (AN's own advice for BATT/EXT supply).
- Power measurement points: 5 V and 3 V rails each have current-measurement jumpers; the
  module's own rail is measurable at TB J1 / J5 VCC_nRF pin [TB schematic POWER + TEST
  CONNECTORS sheets]. TB silk nets: 3V3 (IB rail), VCC_nRF (module rail).

## 5. Kit / module variants

- Module variant scheme [DS2454 §1 p.4, §6.6 p.21]:
  - **ISP2454-LL**: 32 MHz crystal only; no 32.768 kHz crystal; no 4 MB flash ("not the best
    energy savings" — LF clock is RC).
  - **ISP2454-LX**: adds the 32.768 kHz crystal (better BLE/CS clock sync, ±20 ppm).
  - **ISP2454-LP**: adds a 4 MB QSPI flash on P2.00–P2.05; those six P2 IOs become
    Not Connected on LP [DS2454 §3 p.12–13; option schematic p.11].
  - Only LL/LX share the full pinout; both keep P1.04/P1.05 [DS2454 §3 p.12–13].
- Common hardware (all variants): nRF54L15 (Cortex-M33 + net core), 1524 KB RRAM / 256 KB
  RAM, 8.0 x 8.0 x 1.0 mm 62-pad LGA, DC-DC with load inductor integrated, up to 32 GPIOs,
  1.7–3.6 V single supply, −40..105 °C [DS2454 §1 p.4, §2.3 p.7, §4.1 p.15].
- Antenna: exactly **one integrated PCB antenna** (0.6 dBi typ; EIRP up to 8.6 dBm; TX up to
  +8 dBm; Rx −98 dBm @ BER <0.1%) [DS2454 §2.6–2.7 pp.9]. No u.FL/RF-pad variant exists in
  the ordering scheme. External RF access is by rework: OUT_ANT (pin 20) and OUT_MOD
  (pin 22) route through 0 Ω links (R10, R11) to an **SMA connector (J4, "Conn_SMA")** on
  the TB [DS2454 §3 p.12–13 pin 20/22 note "Pin 20 connected to Pin 22 for normal
  operation"; TB schematic RevB RF net]. Fit state of R10/R11 (internal antenna vs SMA path)
  is a bench check. Antenna keep-out rules for any adapter PCB: 1 mm board edge, metal
  exclusion zones [DS2454 §4.3 p.16].
- a1_4 fit: the module has a single antenna and **no antenna switch**, so the a1_4
  single-antenna preset story (one antenna, `BT_CTLR_SDC_CS_NUM_ANTENNAS=1`, no switching
  callback) maps 1:1 onto any ISP2454 variant. Certification (CE/FCC/IC/TELEC, module mark
  R 020-250242) is against the internal antenna [DS2454 p.1, p.18].
- Module hardware revision is laser-marked: `M/N: ISP2454-TT` + `YY` `WW` + **R** (one-letter
  hardware revision) + build code [DS2454 §6.1 p.18]; the kit page's "nRF54L15 Engineering B"
  warning concerns the die revision inside (bench-check via Programmer/nrfutil device info;
  CS deltas of Eng-B vs production die are Nordic-side, out of scope here).
- EVK/board revisions seen: Interface Board ISP130603 **G** and **H** (PCB silk
  ISP27001A/B), Test Board **isp2454-lx_tb_B schematic Rev 2 (2025-12-05)**, author
  "Cedric Requin", Insight SiP Sophia Antipolis. DS2454 module datasheet is at **R6
  (09/06/2026)**; AN250502 at **R0**. The DS2454_DK one-pager is R0 (nov-2024 photo EXIF).
- Firmware note (DS2454 §2.5 note 2, p.8): for the module's 32.768 kHz crystal, Insight
  recommends **`load-capacitance-femtofarad = <19000>`** on `&lfxo` in the devicetree
  overlay — the `nrf54l15dk` board target ships 17000 [local NCS v3.4.0:
  `boards/nordic/nrf54l15dk/nrf54l_05_10_15_cpuapp_common.dtsi:34-37`]. Any a1_4 build on
  the EVK (unmodified DK target) should carry the 19 pF overlay for the −20/+20 ppm LFXO
  class the CS timing budget assumes.

## 6. Cross-check hooks for the rig bring-up

| # | Doc claim (source) | Bench check |
|---|---|---|
| 1 | Kit = ISP2454-LX-EB: IB + TB pair (AN250502 §2.1; DS2454_DK p.1) | Silk: TB "ISP2454-LX_TB" + rev letter; IB "ISP27001A/B Interface Board"; look for ISP130603 rev (G/H) in copper/silk |
| 2 | Variant = LX on the TB | Read module laser mark: `M/N: ISP2454-` **TT** field (expect LX) + `R` hardware-revision letter (DS2454 §6.1) |
| 3 | J-Link OB on the IB; target = single nRF54L15 SWD | Plug IB USB-B: expect SEGGER (0x1366) J-Link enumeration in `lsusb`; note whether a second CDC/VCOM interface appears (OB-UART existence test) |
| 4 | RTT + `nrfutil device` usable over the OB | `nrfutil device list` sees the EVK; quick JLinkExe connect with `-Device CORTEX-M33` (old-DLL workaround) and an RTT read |
| 5 | uart20 = P1.04 TX / P1.05 RX on IB header P36/P37 (AN250502 p.18/21, table p.7) | Continuity: IB silk P36 ↔ module pad 40 (P1.04), P37 ↔ pad 38 (P1.05); then FT232 at 921600 8N1 COBS smoke test on P36/P37 |
| 6 | Module rail = 3.0 V by default (J4=REG) — not 1.8 V like the DK | Measure VCC_nRF on TB J5/J6 test connector with J4=REG; decide FT232 IO level (3.0 V IO vs J4=EXT @ 1.8 V) before wiring |
| 7 | TB J1 = 3V3→VCC_nRF isolation; IB UART/SWD jumpers = line isolation | Eyeball J1 fitted; verify no VCC on the FT232 pigtail (Raytac backfeed lesson); power-off/backfeed test: jig off → VCC_nRF must fall to 0 |
| 8 | OB unpowered when J4=BATT/EXT (AN250502 p.5) | Flip J4 to EXT, re-check `nrfutil device list` (expect OB gone) — and remember to restore J4=REG for flashing |
| 9 | SMA (J4) via R10/R11 0 Ω, internal antenna otherwise (TB schematic RevB) | Which of R10/R11 is populated; keep SMA path in mind for conducted CS TX power checks |
| 10 | LFXO should use 19 pF on this module (DS2454 §2.5 n.2) | Confirm the a1_4 build overlay sets `load-capacitance-femtofarad = <19000>` (DK target default is 17000) |
| 11 | Die = nRF54L15 Engineering B (shop warning) | `nrfutil device info` / Programmer: read die revision; log it in the bench ticket |
| 12 | SB2/SB3 tie printed NFC antenna to P1.02/P1.03 (TB schematic NFC sheet) | Loupe the solder bridges if P1.02/P1.03 are ever repurposed (they sit on IB header pins P34/P35) |

### Unconfirmed — verify on bench (summary)

- Cable count/contents of the EB box (shop says "cables as listed in the datasheet"; not itemized).
- Existence and pin-mapping of the OB's UART/VCOM lines (the "UART & SWD enable jumpers" net to which module UART — could not be confirmed without the ISP130603 schematic, which is not published).
- Default positions of J2/J4/SWD-UART jumpers/TB J1 (read from AN photos, not stated in text).
- RTT support on this specific OB firmware; minimum J-Link software version for nRF54L15 (AN only says "latest").
- 921600 baud signal integrity on the IB P36/P37 header path (AN examples only run 115200/19200).
- R10/R11 population state on the TB (internal antenna vs SMA), and which TB rev letter is in hand.
