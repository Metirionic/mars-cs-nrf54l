# Flash prebuilt firmware (quickstart)

This guide is for evaluators who want to run Channel Sounding by flashing
prebuilt firmware and never build anything. To build from source instead,
see [docs/build-from-source.md](build-from-source.md).

## What you need

- Two nRF54L15 DK boards (Channel Sounding needs a pair — see
  [Channel Sounding needs two boards](#channel-sounding-needs-two-boards)).
- A USB cable for each DK (the onboard J-Link is used for flashing and
  exposes the serial ports — see
  [Read the ranging output](#read-the-ranging-output)).
- A host flashing tool: the Nordic GUI Programmer ([nRF Connect for
  Desktop](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-desktop))
  or the `nrfjprog` CLI from Nordic's command-line tools.

## Channel Sounding needs two boards

Channel Sounding is a two-device protocol. You must flash **one board as the
initiator and a second board as the reflector** — flashing only one board
yields nothing. This guide uses the recommended starting pairs on the nRF54L15 DK — RAS and
IPT, peer choices (see
[docs/architecture.md](architecture.md#ranging-data-flow) for what differs
between the modes). Flash a **matched** pair — both boards in the same mode
(RAS **or** IPT); the two modes use different BLE discovery and won't pair
with each other:

| Mode | Role | Preset | Antennas | Paths |
|------|------|--------|----------|-------|
| RAS | Initiator (BLE Central) | `nrf54l15dk_cent_a1_1` | 1 | 1 |
| RAS | Reflector (BLE Peripheral) | `nrf54l15dk_peri_a1_4` | 1 | 4 |
| IPT | Initiator (BLE Central) | `nrf54l15dk_cent_a1_1_ipt` | 1 | 1 |
| IPT | Reflector (BLE Peripheral) | `nrf54l15dk_peri_a1_4_ipt` | 1 | 4 |

Preset names follow `<board>_<cent|peri>_a<antennas>_<paths>`, with an `_ipt`
suffix for IPT: `cent`/`peri` is the BLE role, `a1` is the antenna count, and
the trailing number is the antenna-path count. Other presets for third-party
boards ship in the same archive; see the [preset table](hardware.md#presets).

## Download the firmware

1. Go to the repo's **Releases** page on GitHub and download
   `cs-ranging-firmware.zip` from the latest release.
2. Unzip it. The archive is flat — 22 `.hex` files at the root, named
   `<role>_<preset>.hex`. That is six RAS presets and five `_ipt` presets per
   role (11 initiator + 11 reflector); no build step is needed.
3. Locate the two files for your chosen pair. IPT files use the same
   `<role>_<preset>.hex` name with an `_ipt` suffix before `.hex`:
   - **RAS pair** — `initiator_nrf54l15dk_cent_a1_1.hex` +
     `reflector_nrf54l15dk_peri_a1_4.hex`
   - **IPT pair** — `initiator_nrf54l15dk_cent_a1_1_ipt.hex` +
     `reflector_nrf54l15dk_peri_a1_4_ipt.hex`

The repo ships no prebuilt `.hex` files itself — the release archive is the
only source.

## Flash the boards

Flash the initiator hex to one DK and the reflector hex to the other. Connect
each DK by USB (its onboard J-Link), then use either path below.

### Option A — Nordic GUI Programmer (recommended)

1. Open
   [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-desktop)
   and install the **Programmer** app.
2. Launch Programmer and select the DK's onboard J-Link from the device
   drop-down.
3. Click **Add file** and choose the matching `.hex` for your chosen pair —
   the initiator file for the initiator board, the reflector file for the
   reflector board (e.g. `initiator_nrf54l15dk_cent_a1_1.hex` and
   `reflector_nrf54l15dk_peri_a1_4.hex` for RAS; the same names with `_ipt`
   for IPT).
4. Click **Erase & write** to flash. Repeat on the second board with its file.

### Option B — `nrfjprog` CLI (for scripted use)

```bash
# RAS pair — initiator board, then reflector board:
nrfjprog --program initiator_nrf54l15dk_cent_a1_1.hex --verify --reset
nrfjprog --program reflector_nrf54l15dk_peri_a1_4.hex --verify --reset

# IPT pair — same names with the _ipt suffix:
nrfjprog --program initiator_nrf54l15dk_cent_a1_1_ipt.hex --verify --reset
nrfjprog --program reflector_nrf54l15dk_peri_a1_4_ipt.hex --verify --reset
```

The modern alternative is `nrfutil device program --firmware <file>.hex` from
the nRF Connect Device Command Line Tool (it auto-selects a connected device,
or pass `--device <serial-number>`).

## Read the ranging output

Only the **initiator** streams ranging results, on a dedicated COBS UART. The
reflector is silent on this UART. The DK routes both this UART and the
console/log UART to its onboard J-Link, which exposes them as two virtual COM
ports over the single debug USB cable — no external USB-to-serial adapter is
needed.

With the **initiator** DK connected by its debug USB, two virtual COM ports
appear on the host:

| Virtual COM port | DK UART | Pins | Carries |
|------------------|---------|------|---------|
| Serial Port 0 (1st) | uart30 | P0.00 / P0.01 | console / log output |
| Serial Port 1 (2nd) | uart20 | P1.04 / P1.05 | ranging stream (CSV) |

Open **Serial Port 1** (the second virtual COM port) at **921600 baud, 8N1, no
flow control**:

```bash
picocom -b 921600 /dev/ttyACM1
```

(`screen /dev/ttyACM1 921600`, or on Windows PuTTY/Tera Term, work too. The
exact device name and number depend on your OS and how many devices are
connected — pick the second of the two ports the DK enumerates.)

The ranging stream is **plain CSV text** — one line per Mode-2 step:
`channel, magnitude, phase` (IPT) or
`channel, magnitude, phase, reflector magnitude, reflector phase` (RAS), with
magnitude `sqrt(i*i + q*q)` and phase `atan2(q, i)` in radians (`%.6f`). Lines
within a procedure are sorted by channel ascending; a blank line separates
procedures. It is ASCII, not binary — read it directly in `picocom` or capture
it to a file for plotting/analysis.

> **nRF54L15 TAG is different.** The quickstart above assumes the DK, which
> routes both UARTs to its onboard J-Link over the debug USB cable. The TAG has
> **no onboard USB**, so reading the ranging stream requires an external
> UART-to-USB adapter (e.g. FT232R) wired to the COBS UART: `uart20`
> **TX → P1.13**, **RX → P1.14** (`921600`, 8N1, no flow control) — the pin
> assignment in the shared `boards/uart20_p1_13_p1_14.dtsi` (included by
> `boards/nrf54l15tag_nrf54l15_cpuapp.overlay`). The TAG is
> flashed through an nRF54L15 DK's `DEBUG OUT` header. See
> [docs/hardware.md](hardware.md#nrf54l15-tag-wiring-notes) for the full TAG
> wiring notes.

## Troubleshooting

- **Only see logs, no ranging bytes** — you opened Serial Port 0 (console)
  instead of Serial Port 1 (ranging). Open the second of the two virtual COM
  ports the DK enumerates.
- **Serial Port 1 is silent (right port, nothing on it)** — only the
  **initiator** emits the ranging stream; the reflector emits console
  logs (Serial Port 0) but never writes to the ranging UART. If the board you are
  reading from is the reflector, flash the initiator hex to it. If you are
  already on the initiator, see *Two boards but no stream* below.
- **Garbage / wrong bytes** — confirm the baud is 921600, 8N1, and that you
  opened Serial Port 1 (P1.04/P1.05), not Serial Port 0 (P0.00/P0.01, logs).
  If your terminal refuses 921600 or shows only garbage, update the DK's
  J-Link OB firmware via nRF Connect for Desktop (older OB firmware lists
  921600 as unsupported on the virtual COM ports).
- **Nothing happens** — Channel Sounding needs the initiator **and** the
  reflector running. Power and flash both boards.
- **Two boards but no stream** — confirm both boards are powered and within
  BLE range; the initiator only emits while a CS procedure is running.

## Next steps

- Build from source: see [docs/build-from-source.md](build-from-source.md).
- All available presets (including third-party boards): see the
  [preset table](hardware.md#presets).
- Hardware reference (boards, UARTs, antenna-switch GPIOs, antenna/path preset
  mapping): see [docs/hardware.md](hardware.md).
- A host decoder for the COBS ranging stream is tracked as future work.
