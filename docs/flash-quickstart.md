# Flash prebuilt firmware (quickstart)

This guide is for evaluators who want to run Channel Sounding by flashing
prebuilt firmware and never build anything. To build from source instead,
see the [README](../README.md).

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
yields nothing. This guide uses the recommended starting pair, both on the
nRF54L15 DK:

| Role | Preset | Antennas | Paths |
|------|--------|----------|-------|
| Initiator (BLE Central) | `nrf54l15dk_cent_a1_1` | 1 | 1 |
| Reflector (BLE Peripheral) | `nrf54l15dk_peri_a1_4` | 1 | 4 |

Preset names follow the form `<board>_<cent|peri>_a<antennas>_<paths>`:
`cent`/`peri` is the BLE role, `a1` is the antenna count, and the trailing
number is the antenna-path count. Other presets for third-party boards ship
in the same archive; see the [preset table](hardware.md#presets).

## Download the firmware

1. Go to the repo's **Releases** page on GitHub and download
   `cs-ranging-firmware.zip` from the latest release.
2. Unzip it. The archive is flat — eight `.hex` files at the root, named
   `<role>_<preset>.hex`. No build step is needed.
3. Locate the two files for the recommended pair:
   - `initiator_nrf54l15dk_cent_a1_1.hex`
   - `reflector_nrf54l15dk_peri_a1_4.hex`

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
3. Click **Add file** and choose the matching `.hex`:
   `initiator_nrf54l15dk_cent_a1_1.hex` for the initiator board,
   `reflector_nrf54l15dk_peri_a1_4.hex` for the reflector board.
4. Click **Erase & write** to flash. Repeat on the second board with its file.

### Option B — `nrfjprog` CLI (for scripted use)

```bash
# Initiator board:
nrfjprog --program initiator_nrf54l15dk_cent_a1_1.hex --verify --reset

# Reflector board:
nrfjprog --program reflector_nrf54l15dk_peri_a1_4.hex --verify --reset
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
| Serial Port 1 (2nd) | uart20 | P1.04 / P1.05 | ranging stream (COBS) |

Open **Serial Port 1** (the second virtual COM port) at **921600 baud, 8N1, no
flow control**:

```bash
picocom -b 921600 /dev/ttyACM1
```

(`screen /dev/ttyACM1 921600`, or on Windows PuTTY/Tera Term, work too. The
exact device name and number depend on your OS and how many devices are
connected — pick the second of the two ports the DK enumerates.)

The ranging stream is **COBS-framed binary** — postcard-serialized structs
wrapped in COBS, produced by the external
[mars-bluetooth-hci](https://github.com/Metirionic/mars-bluetooth-hci)
library. A raw terminal shows binary bytes, not readable text. A host
decoder for this wire format is not yet shipped with this repo and will be
covered separately; this guide covers capture only.

> **nRF54L15 TAG is different.** The quickstart above assumes the DK, which
> routes both UARTs to its onboard J-Link over the debug USB cable. The TAG has
> **no onboard USB**, so reading the COBS ranging stream requires an external
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
  **initiator** emits the COBS ranging stream; the reflector emits console
  logs (Serial Port 0) but never writes to the COBS UART. If the board you are
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

- Build from source: see the [README](../README.md).
- All available presets (including third-party boards): see the
  [preset table](hardware.md#presets).
- Hardware reference (boards, UARTs, antenna-switch GPIOs, antenna/path preset
  mapping): see [docs/hardware.md](hardware.md).
- A host decoder for the COBS ranging stream is tracked as future work.
