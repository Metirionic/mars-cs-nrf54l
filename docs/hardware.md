# Hardware reference

Find your board below for programming, power, UART, and antenna connections.
Use the [flash quickstart](flash-quickstart.md) to install prebuilt firmware or
[build from source](build-from-source.md) to compile a preset.

## Supported boards

The measurement UART carries the initiator's COBS-framed binary stream. All
UARTs listed here run at **921600 baud, 8N1, no flow control**. SEGGER RTT (Real-Time Transfer) carries
console logs through the debug probe rather than a serial port.

| Board | Measurement UART | Console | Antenna-switch GPIOs |
|-------|------------------|---------|----------------------|
| [nRF54L15 DK](#nrf54l15-dk) | `uart20`, onboard USB bridge | `uart30` | P1.09, P1.10 |
| [nRF54L15 TAG](#nrf54l15-tag-wiring-notes) | `uart20`, external adapter | SEGGER RTT | P1.09, P1.10 |
| [u-blox EVK-NORA-B2](#u-blox-evk-nora-b2) | `uart30` | `uart20` | P1.09, P1.08 |
| [Ezurio BL54L15u](#ezurio-bl54l15u) | `uart20` | `uart30` | P1.09, P1.08 |
| [Fanstel BM15C](#fanstel-bm15c) | `uart20` | `uart30` | None |
| [Minewsemi ME54BE01](#minewsemi-me54be01-wiring-notes) | `uart20`, onboard USB bridge | SEGGER RTT | None |
| [Raytac AN54LV-K15](#raytac-an54lv-k15-wiring-notes) | `uart20`, external adapter | SEGGER RTT | P0.04, P0.03 |
| [Insight SiP ISP2454](#insight-sip-isp2454-wiring-notes) | `uart20`, external adapter | SEGGER RTT | None |
| [Würth Ophelia-IV MiniEV](#würth-ophelia-iv-miniev-wiring-notes) | `uart20`, external adapter | SEGGER RTT | None |
| [KAGA FEI EC4L15BA1](#kaga-fei-ec4l15ba1-wiring-notes) | `uart20`, onboard USB bridge | SEGGER RTT | None |

## Wiring and board notes

For an external UART adapter, connect **board TX → adapter RX** and a common
GND. Connect adapter TX → board RX only if needed. Use logic levels compatible
with the board supply; power connections are described separately below.
Pin names such as P1.04 mean GPIO port 1, pin 4.

### nRF54L15 DK

Use the debug USB connector for programming and serial access. The onboard
J-Link exposes two virtual serial ports:

| Port | Function | GPIO pair |
|------|----------|-----------|
| Serial Port 0 | Console, `uart30` | P0.00 / P0.01 |
| Serial Port 1 | Measurements, `uart20` | P1.04 / P1.05 |

See [reading the ranging output](flash-quickstart.md#read-the-ranging-output)
for host connection instructions.

### nRF54L15 TAG wiring notes

- **Programming:** external debug probe, such as an nRF54L15 DK's `DEBUG OUT`.
- **Power:** DK debug connection or CR2032 cell, following the board guide.
- **Measurements:** external USB-to-UART adapter; board TX P1.13, RX P1.14.
- **Console:** SEGGER RTT through the debug probe. There is no onboard USB bridge.

See the [TAG board guide](https://docs.nordicsemi.com/bundle/ncs-latest/page/zephyr/boards/nordic/nrf54l15tag/doc/index.html).

### u-blox EVK-NORA-B2

Measurements use **uart30**; console logs use **uart20**. Use the carrier-board
documentation for connector pinouts, USB serial-port mapping, power, and debug
connections.

See the [NORA-B2 series data sheet](https://content.u-blox.com/sites/default/files/documents/NORA-B2_DataSheet_UBXDOC-1023859458-38975.pdf)
and [EVK-NORA-B2 user guide](https://content.u-blox.com/sites/default/files/documents/EVK-NORA-B2-openCPU_UserGuide_UBXDOC-465451970-3395.pdf).
Antenna connections depend on the module variant: integrated PCB antenna or
external antenna through an RF connector. Use the `ublox_*` presets.

### Ezurio BL54L15u

Measurements use **uart20**; console logs use **uart30**. Use the carrier-board
documentation for connector pinouts, USB serial-port mapping, power, and debug
connections.

See the [Ezurio BL54L15u data sheet](https://www.ezurio.com/documentation/datasheet-bl54l15u).

### Fanstel BM15C

The measurement UART uses **TX P1.13 / RX P1.14**. Console logs use uart30.
Use the carrier documentation for connector and programming details. This is a
single-antenna configuration without an antenna-switch node; the overlay sets
the LFXO load capacitance to 15.5 pF.

See the [Fanstel BM15C series data sheet](https://www.fanstel.com/s/BM15C-Product-Specifications-m9rn.pdf).

### Minewsemi ME54BE01 wiring notes

- **Programming:** external J-Link or DK `DEBUG OUT`; module SWDIO P0.02,
  SWDCLK P0.03, plus the probe's required reference and ground connections.
- **Power and measurements:** onboard Type-C USB connection provides both.
  The firmware uses module TX P1.04 / RX P1.05.
- **Console:** SEGGER RTT through the external probe.

See the [development-board data sheet](https://store.minewsemi.com/wp-content/uploads/2024/11/Development_Board_ME54BE01_Datasheet_EN.pdf)
and [module data sheet](https://store.minewsemi.com/wp-content/uploads/2025/02/ME54BS01-nRF54L15_Datasheet_K_EN.pdf).

### Raytac AN54LV-K15 wiring notes

- **Programming and console:** external J-Link or DK `DEBUG OUT`; console over SEGGER RTT.
- **Measurements:** external USB-to-UART adapter on J1, board TX P1.04 / RX P1.05.
- **Power:** J1 can supply the board from the adapter. Use one power source;
  adding another supply can backfeed the rail. Check the adapter's supply
  capacity and logic levels against the board and RF-switch supply.
- **Antennas:** three RF connectors selected by the SKY13586-678LF switch.
  Control V2 is P0.04 (index bit 0); V1 is P0.03 (index bit 1).
  Antenna indices 0, 1, 2 select **RF3, RF2, RF1**, respectively.

See [Raytac's module documentation](https://www.raytac.com/product/ins.php?index_id=169)
and the [RF-switch data sheet](https://www.skyworksinc.com/-/media/SkyWorks/Documents/Products/2401-2500/SKY13586-678LF_203452G.pdf).

### Insight SiP ISP2454 wiring notes

These notes refer to the ISP2454-LX-EB kit: the module Test Board mounted on
an ISP130603 Interface Board.

- **Programming:** onboard J-Link OB through USB. If access protection blocks
  the first flash, recover the device first. The tested J-Link 7.92m tools
  failed to attach to this debugger; `nrfutil device` worked.
- **Measurements:** external UART adapter on Interface-Board **P36 (TX,
  P1.04)** and **P37 (RX, P1.05)**. The onboard J-Link's serial port does not
  carry this stream.
- **Power:** default J4 = REG supplies the module at 3.0 V. Connect the UART
  adapter's GND and signals only; leave its VCC disconnected to avoid backfeeding.
- **Console:** SEGGER RTT through the onboard debugger.
- **Antenna:** integrated PCB antenna. SMA access requires changing the 0 Ω links.
- **Clock:** the overlay uses an 18 pF LFXO load, the SoC maximum; the module
  vendor specifies 19 pF. Confirm timing if changing clock settings.
- **Silicon:** tested kits used Engineering-B silicon; record the revision
  when comparing measurements across boards.

See the [kit data sheet](https://www.insightsip.com/fichiers_insightsip/pdf/ble/ISP2454/isp_ble_DS2454_DK.pdf)
and [kit application note](https://www.insightsip.com/fichiers_insightsip/pdf/ble/ISP2454/isp_ble_AN250502.pdf).

### Würth Ophelia-IV MiniEV wiring notes

These notes apply to MiniEV 2621119022001 with module 2621011022000.

| Connection | Pins |
|------------|------|
| Measurement UART, CON4 | Pin 9 = TX P1.04; pin 8 = RX P1.15 |
| Power, CON4 | Pin 7 = 5 V input; pins 1–5 = GND |
| Unused flow control, CON4 | Pin 6 = RTS P1.06; pin 10 = CTS P1.07 |
| SWD, CON2 | Pin 1 = VTref; pin 7 = SWDIO; pin 9 = SWCLK; pin 15 = reset; even pins 4–20 = GND; pin 2 unconnected |

- **Programming:** external J-Link. CON2 is shipped as bare solder pads;
  fit a header or use suitable probes. Select the debugger by serial number
  when multiple probes are attached.
- **Power:** with 5 V on CON4, retain the factory CON3 jumpers at 1–2 and 5–6.
  The regulator supplies approximately 3.0 V. Do not feed external module
  power into CON3 pins 2–5 while the 5–6 regulator jumper is fitted.
- **Measurements:** external UART adapter. Connect its RX to CON4 pin 9 and
  GND to the board. If connecting adapter TX to pin 8, use 3.3 V logic.
- **Console:** SEGGER RTT through the debug probe.
- **Clock:** internal RC oscillator; the board ships without the LFXO parts.
  Keep the preset's RC-clock configuration unless the crystal circuit is fitted.
- **Antenna:** integrated PCB antenna. SMA access requires fitting CON1 and
  C1 and removing C2. The MiniEV also lacks the SPI NOR modeled by its base
  board; the firmware overlay disables that interface.

The tested setup required a hard reset after flashing. Its programming command is:

```bash
nrfutil device program --firmware <firmware.hex> --serial-number <SN> \
  --options chip_erase_mode=ERASE_RANGES_TOUCHED_BY_FIRMWARE,reset=RESET_HARD
```

This erase mode leaves bytes beyond the new image's ranges untouched. Check
programming completes and the new application starts.

See the [MiniEV manual](https://www.we-online.com/components/products/manual/UM_Ophelia-IV_MiniEV_262111902xxxx%20(rev1.0).pdf)
and [Zephyr application note](https://www.we-online.com/ANR036).

### KAGA FEI EC4L15BA1 wiring notes

- **Programming:** external probe on CN1. Its pinout differs from the standard
  ARM 10-pin header: **1 = VDD/VTref, 2 = SWDIO, 4 = SWDCLK, 3/5/9 = GND**.
  There is no reset signal on CN1. Adapt the connection pin by pin.
- **First flash:** factory modules are access-protected; recover before flashing.
- **Power and measurements:** CN6 mini-USB supplies both through the onboard
  FT232RNQ. UART is board TX P1.04 / RX P1.05 via SB1/SB2; disable flow control.
- **Power selection:** default CN2 connects the bridge's 3.3 V supply to the
  module. Cut CN2 pins 1–2 before using an external 1.7–3.6 V supply. UART
  logic levels follow the module rail through SB5. The module supports DC/DC
  regulator operation only.
- **Headers:** CN1 and CN6 are fitted; CN4/CN5/CN7/CN8/CN9 need headers soldered
  before use. CN8 also exposes SWD, UART, and power.
- **Console:** SEGGER RTT through the external probe.
- **Antenna:** integrated PCB antenna; external U.FL access requires JP1/CN7
  rework. CN7 is not fitted.
- **Module variant:** confirm the shield says EC4L15BA1. The ES4L15BA1 sibling
  lacks its internal 32.768 kHz crystal and needs a different clock setup.
  The EC4L15BA1 overlay retains the DK's LFXO load setting.
- **Silicon:** tested modules used Engineering-B silicon.

See the [module data sheet](https://www.kagafei.com/jp/products/wireless-modules/bluetooth/File/__icsFiles/afieldfile/2026/06/29/EC4L15BA1_EC4L10BA1_EC4L05BA1_DataSheet_V1_1_20260612E.pdf)
and [EVB manual](https://www.kagafei.com/jp/products/wireless-modules/bluetooth/File/__icsFiles/afieldfile/2026/02/12/EC4L15BA1_EVBManual_V1_0_20260126.pdf).

### Checking serial and console connections

Identify USB adapters by device identity rather than assuming a fixed port
number. A J-Link virtual serial port does not necessarily carry the module UART.
For a silent measurement stream, check the initiator/reflector pair, firmware
mode, adapter wiring, and host byte reception before relying on UART LEDs.

SEGGER RTT buffers can retain old logs across resets and reflashing. Keep a SEGGER RTT
client connected to drain live output; power-cycle the board if stale boot
messages make it unclear which image is running.

## Antenna and path presets

Preset names use `<board>_<cent|peri>_a<antennas>_<paths>`, with `_ipt` for
Inline PCT Transfer. `cent` means initiator; `peri` means reflector. Without
`_ipt`, the preset uses RAS (Ranging Service). Use the same mode on both devices.

`a2_4`, for example, configures two local antennas and a four-path capacity.
The actual path count depends on both devices' antenna capabilities; the
firmware negotiates the peer count. A path limit of four does not mean the
board has four physical antennas.

### Presets

The table lists build presets; release archives contain a subset. Each name
maps to its board overlay and configuration in the application's
[initiator presets](../initiator/CMakePresets.json) and
[reflector presets](../reflector/CMakePresets.json).

Find the preset for each board and role independently. Both devices must use
the same mode (RAS or IPT), but board types and antenna counts may differ.
Rows group presets with matching settings; they do not define required
measurement pairs.

| Module or board | Mode | Antennas / path limit | Initiator preset | Reflector preset |
|-----------------|------|----------------------|------------------|------------------|
| nRF54L15 DK | RAS | A1 / 4 | `nrf54l15dk_cent_a1_4` | `nrf54l15dk_peri_a1_4` |
| nRF54L15 DK | IPT | A1 / 4 | `nrf54l15dk_cent_a1_4_ipt` | `nrf54l15dk_peri_a1_4_ipt` |
| nRF54L15 TAG | RAS | A2 / 4 | `nrf54l15tag_cent_a2_4` | `nrf54l15tag_peri_a2_4` |
| nRF54L15 TAG | IPT | A2 / 4 | `nrf54l15tag_cent_a2_4_ipt` | `nrf54l15tag_peri_a2_4_ipt` |
| u-blox EVK-NORA-B2 | RAS | A1 / 4 | `ublox_cent_a1_4` | `ublox_peri_a1_4` |
| u-blox EVK-NORA-B2 | IPT | A1 / 4 | `ublox_cent_a1_4_ipt` | `ublox_peri_a1_4_ipt` |
| Ezurio BL54L15u | RAS | A2 / 4 | `ezurio_bl54l15u_cent_a2_4` | `ezurio_bl54l15u_peri_a2_4` |
| Ezurio BL54L15u | IPT | A2 / 4 | `ezurio_bl54l15u_cent_a2_4_ipt` | `ezurio_bl54l15u_peri_a2_4_ipt` |
| Fanstel BM15C | RAS | A1 / 4 | `fanstel_bm15c_cent_a1_4` | `fanstel_bm15c_peri_a1_4` |
| Fanstel BM15C | IPT | A1 / 4 | `fanstel_bm15c_cent_a1_4_ipt` | `fanstel_bm15c_peri_a1_4_ipt` |
| Minewsemi ME54BE01 | RAS | A1 / 4 | `minew_me54be01_cent_a1_4` | `minew_me54be01_peri_a1_4` |
| Minewsemi ME54BE01 | IPT | A1 / 4 | `minew_me54be01_cent_a1_4_ipt` | `minew_me54be01_peri_a1_4_ipt` |
| Raytac AN54LV-K15 | RAS | A3 / 4 | `raytac_an54lv_k15_cent_a3_4` | `raytac_an54lv_k15_peri_a3_4` |
| Raytac AN54LV-K15 | IPT | A3 / 4 | `raytac_an54lv_k15_cent_a3_4_ipt` | `raytac_an54lv_k15_peri_a3_4_ipt` |
| Insight SiP ISP2454 | RAS | A1 / 4 | `insight_isp2454_cent_a1_4` | `insight_isp2454_peri_a1_4` |
| Insight SiP ISP2454 | IPT | A1 / 4 | `insight_isp2454_cent_a1_4_ipt` | `insight_isp2454_peri_a1_4_ipt` |
| Würth Ophelia-IV MiniEV | RAS | A1 / 4 | `ophelia4ev_cent_a1_4` | `ophelia4ev_peri_a1_4` |
| Würth Ophelia-IV MiniEV | IPT | A1 / 4 | `ophelia4ev_cent_a1_4_ipt` | `ophelia4ev_peri_a1_4_ipt` |
| KAGA FEI EC4L15BA1 | RAS | A1 / 4 | `kaga_ec4l15ba1_cent_a1_4` | `kaga_ec4l15ba1_peri_a1_4` |
| KAGA FEI EC4L15BA1 | IPT | A1 / 4 | `kaga_ec4l15ba1_cent_a1_4_ipt` | `kaga_ec4l15ba1_peri_a1_4_ipt` |

### Configuration files

Board overlays in `boards/` select UARTs, pins, antenna-switch
GPIOs, and board-specific peripherals. Most presets use the nRF54L15 DK base
board; the TAG and MiniEV use their own board definitions. Let the preset
select the base board and overlay together.

The `*_local.conf` fragments set local antenna count and path limits:

| Fragment | Local antennas | Path limit |
|----------|----------------|------------|
| `2_path_1_local.conf` | 1 | 2 |
| `2_path_2_local.conf` | 2 | 2 |
| `4_path_1_local.conf` | 1 | 4 |
| `4_path_2_local.conf` | 2 | 4 |
| `4_path_3_local.conf` | 3 | 4 |
| `4_path_4_local.conf` | 4 | 4 |

Carrier `.conf` files configure console and driver requirements. IPT presets
also include the shared and role-specific `inline_pct_*.conf` fragments.
The initiator's `central.overlay` is a Kconfig fragment despite its extension.

The Nordic controller drives the antenna switch. The DK, U-Blox, Ezurio, and
Raytac overlays use binary-index switching (`multiplexing-mode = <1>`); the TAG
uses mode 0. Fanstel, Minewsemi, Insight SiP, MiniEV, and KAGA FEI presets use a
single antenna without an antenna-switch node. See the Raytac notes above for
its connector mapping.
