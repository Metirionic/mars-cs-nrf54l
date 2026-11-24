# mars-cs-nrf54l

Bluetooth® Channel Sounding firmware for Nordic nRF54L15-based boards, built
with nRF Connect SDK v3.4.1. An initiator and a reflector collect ranging
measurements; the initiator streams them to a host over UART.

## Get started

You need two supported boards: one initiator and one reflector, both using the
same ranging mode.

- [Flash prebuilt firmware](docs/flash-quickstart.md) to try the firmware without a build setup.
- [Build from source](docs/build-from-source.md) to develop or customize the firmware.
- [Choose your board and presets](docs/hardware.md) for wiring, UART connections, and hardware settings.

To view live distances and export measurements, use the
[Metirionic Channel Sounding Evaluation Application](https://github.com/Metirionic/mars-ranging-demo).

You can [set a reflector's X/Y location](docs/reflector-location.md) from
nRF Connect for Mobile and save it in the reflector's internal flash.

## Firmware variants

| Role | Responsibility |
|------|----------------|
| Initiator (BLE central) | Connects to the reflector, configures ranging, and streams measurements to the host. |
| Reflector (BLE peripheral) | Advertises and participates in ranging procedures. |

Two acquisition modes are available:

- **RAS (Ranging Service):** the reflector transfers its measurements over GATT
  for the initiator to combine with its local data.
- **IPT (Inline PCT Transfer):** the reflector's phase contribution is embedded
  in the initiator's local tone measurements, avoiding a separate RAS data transfer.

Both modes produce the same COBS-framed measurement format through
[mars-bluetooth-hci](https://github.com/Metirionic/mars-bluetooth-hci).
Board types and antenna counts may differ between devices; see the
[hardware reference](docs/hardware.md#presets) for available configurations.

## Development

- [Architecture](docs/architecture.md) — component responsibilities, measurement flow, and execution model.
- [Contributing](docs/contributing.md) — development conventions, checks, and pull requests.

## License

[MIT](LICENSE)
