# mars-cs-nrf54l

Nordic nRF54L15 Channel Sounding firmware samples. Runs on top of
[nRF Connect SDK v3.4.0](https://developer.nordicsemi.com/nRF_Connect_SDK/)
and uses [mars-bluetooth-hci](https://github.com/Metirionic/mars-bluetooth-hci)
for COBS serialization of CS measurement data.

## Get started

Channel Sounding needs a pair of boards — one initiator, one reflector. Pick a path:

- **Flash prebuilt firmware** — no build step; flash and run. See [docs/flash-quickstart.md](docs/flash-quickstart.md).
- **Build from source** — native toolchain + build. See [docs/build-from-source.md](docs/build-from-source.md).

## Firmware variants

| Target | Description |
|--------|-------------|
| `initiator` | BLE Central that connects to a CS reflector, collects ranging data, and outputs COBS-encoded binary over UART |
| `reflector` | BLE Peripheral that advertises and responds to CS procedures |

## Presets at a glance

The recommended starting pair on the nRF54L15 DK:

| Role | Preset | Antennas | Paths |
|------|--------|----------|-------|
| Initiator (BLE Central) | `nrf54l15dk_cent_a1_1` | 1 | 1 |
| Reflector (BLE Peripheral) | `nrf54l15dk_peri_a1_4` | 1 | 4 |

Preset names follow `<board>_<cent|peri>_a<antennas>_<paths>`. For all boards and
antenna/path configurations, see [docs/hardware.md](docs/hardware.md#presets).

## Hardware reference

For board overlays, UART assignments, and the full antenna/path↔preset mapping, see
[docs/hardware.md](docs/hardware.md).

## Architecture

For the firmware module map, ranging-data flow, and initiator main loop, see
[docs/architecture.md](docs/architecture.md).

## Contributing

For the commit convention, pre-commit hooks, and pull-request flow, see
[docs/contributing.md](docs/contributing.md).

## License

MIT
