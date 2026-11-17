# Flash prebuilt firmware

Use this guide to run Channel Sounding on two nRF54L15 DKs with prebuilt
firmware. To compile your own firmware, see [build from source](build-from-source.md).

## What you need

- Two nRF54L15 DKs and a USB cable for each board.
- [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-desktop)
  with its **Programmer** app installed.
- The [Metirionic Channel Sounding Evaluation Application](https://github.com/Metirionic/mars-ranging-demo)
  to view measurements, or a serial application to inspect the raw stream.

For other boards, choose the corresponding firmware from the
[hardware preset table](hardware.md#presets) and follow that board's wiring
notes. Some boards need an external debugger or USB-to-UART adapter.

## Download a firmware pair

Download `cs-ranging-firmware.zip` from the
[GitHub releases page](https://github.com/Metirionic/mars-cs-nrf54l/releases)
and extract it.

Choose **one** pair below. Each pair contains an initiator and a reflector;
both boards must use the same mode and release.

| Mode | Initiator file | Reflector file |
|------|----------------|----------------|
| RAS (Ranging Service) | `initiator_nrf54l15dk_cent_a1_4.hex` | `reflector_nrf54l15dk_peri_a1_4.hex` |
| IPT (Inline PCT Transfer) | `initiator_nrf54l15dk_cent_a1_4_ipt.hex` | `reflector_nrf54l15dk_peri_a1_4_ipt.hex` |

See the [architecture overview](architecture.md#measurement-flow) for how the
two modes acquire measurements.

## Flash the boards

Program one board at a time to keep the roles easy to identify:

1. Connect the first DK through its **debug USB port** and turn it on.
2. Open **Programmer** in nRF Connect for Desktop and select the connected DK.
3. Use **Add file** to load the initiator `.hex` from your chosen pair.
4. Click **Erase & write** and wait for programming to finish.
5. Disconnect that board and repeat with the second DK and the reflector file.

These steps follow Nordic's
[programming procedure](https://docs.nordicsemi.com/r/bundle/nrf-connect-for-desktop/page/app/pc-nrfconnect-programmer/programming_dk.html/general-programming-procedure).
Label the boards, then power both within Bluetooth range. The initiator finds
the reflector and starts ranging automatically.

## Read the ranging output

Use the [Metirionic Channel Sounding Evaluation Application](https://github.com/Metirionic/mars-ranging-demo)
to view live distances and export measurements. See its README for requirements,
downloads, and setup.

### Serial connection

Only the **initiator** sends measurement data. Its debug USB connection exposes
two virtual serial ports:

| Port | UART / pins | Output |
|------|-------------|--------|
| Serial Port 0 (first) | `uart30`, P0.00 / P0.01 | Console logs |
| Serial Port 1 (second) | `uart20`, P1.04 / P1.05 | COBS-framed binary measurements |

Open the initiator's **Serial Port 1** at **921600 baud, 8N1, no flow control**.
No external UART adapter is needed on the DK. To inspect the raw stream with a
terminal instead, for example on Linux:

```bash
picocom -b 921600 --flow n /dev/ttyACM1
```

Replace `/dev/ttyACM1` with the actual port on your computer. Port numbering
varies, especially when both DKs are connected. PuTTY or Tera Term can also
open the port on Windows.

A terminal displays binary bytes, **not readable distances**. Use the evaluation
application above to interpret the measurements. Custom host decoders must match
the firmware's [mars-bluetooth-hci](https://github.com/Metirionic/mars-bluetooth-hci)
wire-format version. Close the terminal before connecting the evaluation
application to the same port.

## Troubleshooting

| Symptom | Check |
|---------|-------|
| Readable logs instead of measurement bytes | Open Serial Port 1 on the initiator; Serial Port 0 carries logs. |
| No measurement bytes | Confirm both boards are powered, within range, and flashed with opposite roles from the same mode and release. Check the initiator's console logs for setup errors. |
| Unreadable characters | Binary output is expected. If a decoder rejects it, check the serial settings and wire-format compatibility. |
| Serial connection cannot use 921600 baud | Update the DK's onboard J-Link firmware using Nordic's tools. |

For board-specific UART connections and programming requirements, see the
[hardware reference](hardware.md).
