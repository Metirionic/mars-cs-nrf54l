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

### Native toolchain install

The above assumes the nRF Connect SDK toolchain is already available. To install
the full native toolchain (host packages, isolated Python venv, west workspace,
and the Zephyr SDK 0.17.4 ARM-only toolchain) from a clean Ubuntu system, run the
codifier script — it is idempotent and safe to re-run:

```bash
bash scripts/install-toolchain.sh             # reflector slice (no Rust)
bash scripts/install-toolchain.sh --with-rust  # also add the initiator's Rust target
```

The `onboarding-verification` CI job runs this exact path from a clean
`ubuntu:24.04` container on every pull request and builds the reflector, so the
documented native install path cannot silently drift.

Build the initiator:

```bash
NCS_DIR=$(pwd) bash ci/build.sh --target initiator --preset nrf54l15dk_cent_a1_1
```

Build the reflector:

```bash
NCS_DIR=$(pwd) bash ci/build.sh --target reflector --preset nrf54l15dk_peri_a1_4
```

`NCS_DIR=$(pwd)` points `ci/build.sh` at the west workspace created above (run from the repo
root). Native-install users: run `source ~/.ncs-venv/bin/activate` first. Users of the
nrfutil toolchain-manager NCS at `/opt/nordic/ncs/current` may drop the `NCS_DIR=$(pwd)`
prefix — it is auto-found.

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

Flashing prebuilt firmware (no build step)? See
[docs/flash-quickstart.md](docs/flash-quickstart.md).

To flash a build you produced locally:

```bash
west flash
```

## License

MIT
