# mars-cs-nrf54l

Nordic nRF54L15 Channel Sounding firmware samples. Runs on top of
[nRF Connect SDK v3.4.0](https://developer.nordicsemi.com/nRF_Connect_SDK/)
and emits CS measurement data as CSV text over UART (no external serialization
library — the former mars-bluetooth-hci COBS path has been removed).

## Get started

Channel Sounding needs a pair of boards — one initiator, one reflector. Pick a path:

- **Flash prebuilt firmware** — no build step; flash and run. See [docs/flash-quickstart.md](docs/flash-quickstart.md).
- **Build from source** — native toolchain + build. See [docs/build-from-source.md](docs/build-from-source.md).

## Firmware variants

Each build targets a BLE role and a ranging mode.

| Target | Description |
|--------|-------------|
| `initiator` | BLE Central that connects to a CS reflector, collects ranging data, and outputs CSV over UART |
| `reflector` | BLE Peripheral that advertises and responds to CS procedures |

The ranging mode is a build-time choice, set by `CONFIG_MARS_CS_INLINE_PCT`:

- **RAS** (Ranging Service) — the initiator pulls the reflector's step data over
  a BLE GATT service and combines it with its own.
- **IPT** (Inline PCT) — the reflector's phase contribution is embedded in
  the local tones, so the initiator uses its own step data only (no GATT service).

Both modes emit the same CSV ranging stream over UART — 3 columns per line in IPT
(`channel, magnitude, phase`), 5 in RAS (`channel, magnitude, phase, reflector
magnitude, reflector phase`), sorted by channel ascending per procedure. See
[docs/architecture.md](docs/architecture.md#ranging-data-flow) for the full
RAS-vs-IPT contrast and per-mode data flow.

## Presets at a glance

The recommended starting pairs on the nRF54L15 DK — RAS and IPT, peer choices:

| Mode | Role | Preset | Antennas | Paths |
|------|------|--------|----------|-------|
| RAS | Initiator (BLE Central) | `nrf54l15dk_cent_a1_1` | 1 | 1 |
| RAS | Reflector (BLE Peripheral) | `nrf54l15dk_peri_a1_4` | 1 | 4 |
| IPT | Initiator (BLE Central) | `nrf54l15dk_cent_a1_1_ipt` | 1 | 1 |
| IPT | Reflector (BLE Peripheral) | `nrf54l15dk_peri_a1_4_ipt` | 1 | 4 |

Preset names follow `<board>_<cent|peri>_a<antennas>_<paths>`, with an `_ipt`
suffix for IPT. For all boards and antenna/path configurations, see
[docs/hardware.md](docs/hardware.md#presets).

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
