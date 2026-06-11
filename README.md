# mars-cs-nrf54l

Nordic nRF54L15 Channel Sounding firmware samples. Runs on top of
[nRF Connect SDK v3.3.0](https://developer.nordicsemi.com/nRF_Connect_SDK/)
and uses [mars-bluetooth-hci](https://github.com/Metirionic/mars-bluetooth-hci)
for COBS serialization of CS measurement data.

## Firmware variants

| Target | Description |
|--------|-------------|
| `initiator` | BLE Central that connects to a CS reflector, collects ranging data, and outputs COBS-encoded binary over UART |
| `reflector` | BLE Peripheral that advertises and responds to CS procedures |

## Prerequisites

- [nRF Connect SDK v3.3.0](https://developer.nordicsemi.com/nRF_Connect_SDK/)
- [west](https://docs.zephyrproject.org/latest/develop/west/install.html)
- Rust toolchain with the `thumbv8m.main-none-eabihf` target (initiator only):
  ```
  rustup target add thumbv8m.main-none-eabihf
  ```

## Building

Clone and set up the west workspace:

```bash
git clone https://github.com/Metirionic/mars-cs-nrf54l.git
cd mars-cs-nrf54l
west init -l . --mf west.yml
west update -o=--depth=1 -n
```

Build the initiator:

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp initiator --preset nrf54l15dk_cent_a1_1
```

Build the reflector:

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp reflector --preset nrf54l15dk_peri_a1_4
```

## Available presets

### Initiator

| Preset | Board | Antennas | Paths |
|--------|-------|----------|-------|
| `nrf54l15dk_cent_a1_1` | nRF54L15DK | 1 | 1 |
| `ublox_cent_a1_1` | U-Blox NINA-B40 | 1 | 1 |
| `ezurio_bl54l15u_cent_a2_4` | Ezurio BL54L15u | 2 | 4 |
| `fanstel_bm15c_cent_a1_1` | Fanstel BM15C | 1 | 1 |

### Reflector

| Preset | Board | Antennas | Paths |
|--------|-------|----------|-------|
| `nrf54l15dk_peri_a1_4` | nRF54L15DK | 1 | 4 |
| `ezurio_bl54l15u_peri_a2_4` | Ezurio BL54L15u | 2 | 4 |
| `fanstel_bm15c_peri_a1_4` | Fanstel BM15C | 1 | 4 |
| `ublox_peri_a1_4` | U-Blox NINA-B40 | 1 | 4 |

## Flashing

```bash
west flash
```

## License

MIT