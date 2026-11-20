# Build from source

This guide uses the native toolchain on Debian/Ubuntu Linux. You need Git,
internet access, and sudo access to install missing system packages. Run the
commands below from the repository root unless stated otherwise.

## Install the toolchain

Clone into a dedicated workspace directory: the installer downloads the Nordic
SDK sources into the repository's **parent directory**.

```bash
mkdir -p ~/mars-workspace
cd ~/mars-workspace
git clone https://github.com/Metirionic/mars-cs-nrf54l.git
cd mars-cs-nrf54l
bash scripts/install-toolchain.sh --with-rust
```

The [installer](../scripts/install-toolchain.sh) sets up the system packages,
Python environment in `.venv/`, pinned nRF Connect SDK, Zephyr toolchain, and Rust target.
It can be rerun if installation is interrupted. Rust is required for the
initiator; omit `--with-rust` if you only build the reflector.

Activate the build environment in each new terminal:

```bash
source .venv/bin/activate
source ~/.cargo/env
```

The second command loads Rust into your shell; skip it for a reflector-only
installation.

## Choose and build a firmware pair

A ranging setup needs one initiator and one reflector using the same mode:
**RAS (Ranging Service)** or **IPT (Inline PCT Transfer)**. Choose one of the
following pairs for the nRF54L15 DK.

### RAS

```bash
bash ci/build.sh --target initiator --preset nrf54l15dk_cent_a1_4
bash ci/build.sh --target reflector --preset nrf54l15dk_peri_a1_4
```

### IPT

```bash
bash ci/build.sh --target initiator --preset nrf54l15dk_cent_a1_4_ipt
bash ci/build.sh --target reflector --preset nrf54l15dk_peri_a1_4_ipt
```

For other boards or antenna configurations, replace the preset names using the
[hardware preset table](hardware.md#presets). See the
[architecture overview](architecture.md#measurement-flow) for the difference
between RAS and IPT.

Each build is stored in `<target>/build_<preset>/`. Rerun the same command
after changing the source. To force a clean rebuild, add `--pristine always`.

To collect flashable `.hex` files in a single directory, add
`--release-dir release` to the build commands. Files are named
`release/<target>_<preset>.hex`.

## Flash your build

Connect the board through its debug USB port. With the J-Link tools installed,
flash the corresponding build directory:

```bash
west flash -d initiator/build_nrf54l15dk_cent_a1_4
west flash -d reflector/build_nrf54l15dk_peri_a1_4
```

Flash each role to a separate board. For IPT builds, use the directories ending
in `_ipt`. For boards without an onboard debugger, use an external debug probe.

See the [flash quickstart](flash-quickstart.md) for flashing tools and
[reading the ranging output](flash-quickstart.md#read-the-ranging-output).
