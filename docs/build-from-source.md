# Build from source

This guide is for developers and contributors building the firmware from
source — the counterpart to [docs/flash-quickstart.md](flash-quickstart.md),
which covers flashing prebuilt firmware with no build step. For the preset and
antenna/path reference, see [docs/hardware.md](hardware.md#presets).

## What you need

The native path targets a Debian/Ubuntu host. The install codifier (next
section) provisions the packages below automatically, so you do not normally
install them by hand — they are listed here so a non-Debian host or a fully
manual install knows what is required:

```bash
sudo apt-get install -y --no-install-recommends \
  build-essential cmake gperf wget ninja-build device-tree-compiler dfu-util \
  git python3 python3-venv curl ca-certificates
```

## Install the toolchain (native path)

From a fresh clone, run the install codifier from the repo root:

```bash
git clone https://github.com/Metirionic/mars-cs-nrf54l.git
cd mars-cs-nrf54l
bash scripts/install-toolchain.sh --with-rust
```

This guide **references**
[scripts/install-toolchain.sh](../scripts/install-toolchain.sh) rather than
duplicating its steps — that script is the single source of truth for the
install. One idempotent invocation hides the entire native toolchain:
host apt packages → an isolated Python venv (`~/.ncs-venv`) → the west
workspace → the Zephyr/NCS pip requirements → the Zephyr SDK (ARM-only; version
auto-detected from `zephyr/SDK_VERSION`, 0.17.4 for NCS v3.3.0) → and, with
`--with-rust`, the `thumbv8m.main-none-eabihf` target.

Every stage guards on existing state, so the script is safe to re-run on an
already-installed venv, workspace, or SDK, or to resume after a partial
failure. Pass `--with-rust` for the initiator (the Rust COBS-serializer
bridge); the reflector-only slice omits the flag. The
`nrfutil toolchain-manager` path is intentionally not used — the pip-distributed
legacy `nrfutil` is broken on Python 3.12.

After install, activate the venv before building (the script's final log prints
this):

```bash
source ~/.ncs-venv/bin/activate
```

The `onboarding-verification` CI job runs this exact command (with
`--with-rust`) from a clean `ubuntu:24.04` container on every pull request and
builds **both** the reflector and the initiator, so the documented native
install path — including the initiator's Rust COBS-serializer build — cannot
silently drift.

## Where the install lands (workspace layout)

The script runs `west init -l . --mf west.yml` from the repo root. With the
`-l` (local manifest) flag, west creates `.west/` and clones the NCS source
tree (`nrf/`, `zephyr/`, `nrfxlib/`, `modules/`, `bootloader/`, `test/`,
`tools/`) in the **parent** of the repo, not inside it — the workspace root is
hardcoded to the manifest directory's parent. Because those checkouts land
outside the repo, `git status` stays clean (`.west/` is gitignored).

The toolchain, the workspace source, and the venv are three separate things on
disk — do not conflate them:

| Thing | Location | Installed by |
|-------|----------|--------------|
| Repo (the manifest / `self` project) | `<parent>/mars-cs-nrf54l/` | `git clone` |
| `.west/` + NCS source (`nrf/`, `zephyr/`, …) | the repo's **parent** dir, outside the repo | `west init -l .` + `west update` |
| Zephyr SDK toolchain (ARM-only) | `~/zephyr-sdk-0.17.4/`, outside both | `west sdk install` |
| Isolated Python venv | `~/.ncs-venv/` | the install codifier |

## Build the firmware

Activate the venv, point `ci/build.sh` at the workspace, and build each app the
same way CI does. The default starting presets are `nrf54l15dk_cent_a1_1`
(initiator) and `nrf54l15dk_peri_a1_4` (reflector) on the nRF54L15 DK:

```bash
source ~/.ncs-venv/bin/activate
NCS_DIR=$(pwd) bash ci/build.sh --target initiator --preset nrf54l15dk_cent_a1_1
NCS_DIR=$(pwd) bash ci/build.sh --target reflector --preset nrf54l15dk_peri_a1_4
```

`ci/build.sh` requires `NCS_DIR` to point at a west workspace. Setting
`NCS_DIR=$(pwd)` from the repo root works because the repo is the manifest
project *inside* the workspace — west walks up from the repo root to the
parent's `.west/`. Users of the nrfutil toolchain-manager NCS at
`/opt/nordic/ncs/current` may drop the `NCS_DIR=$(pwd)` prefix; it is
auto-found by `ci/common.sh`.

Channel Sounding needs **both** an initiator and a reflector, so build and
flash one of each. See
[docs/flash-quickstart.md](flash-quickstart.md#read-the-ranging-output) for the
two-board pairing and how to read the COBS ranging output after flashing a
locally-built image.

### Multiple presets (driven like CI)

`--preset` accepts a comma-separated list, and `--release-dir` collects each
merged `.hex` into a flat output directory. This is exactly how the release
workflow builds every shipped preset:

```bash
NCS_DIR=$(pwd) bash ci/build.sh --target initiator \
  --preset nrf54l15dk_cent_a1_1,ublox_cent_a1_1,ezurio_bl54l15u_cent_a2_4,fanstel_bm15c_cent_a1_1 \
  --release-dir release
NCS_DIR=$(pwd) bash ci/build.sh --target reflector \
  --preset nrf54l15dk_peri_a1_4,ezurio_bl54l15u_peri_a2_4,fanstel_bm15c_peri_a1_4,ublox_peri_a1_4 \
  --release-dir release
```

See [docs/hardware.md](hardware.md#presets) for the full preset table.

### Watch out — the two apps share a build directory by default

A bare `west build <app>` (no `--preset`, no `-d`) writes to a single default
`./build/`. Build the initiator there, then the reflector (or vice-versa), and
west refuses:

```
Build directory "<build_dir>" is for application "<cached_app_dir>", but source
directory "<new_app_dir>" was specified; please clean it, use --pristine, or
use --build-dir to set another build directory
```

The documented path avoids this: `ci/build.sh --target <app> --preset <name>`
builds each preset into its own `binaryDir`
(e.g. `initiator/build_nrf54l15dk_cent_a1_1`,
`reflector/build_nrf54l15dk_peri_a1_4`) and passes `--pristine=auto`, so the two
apps never collide. If you drive `west build` by hand, pass `-d` /
`--build-dir` explicitly to keep the build directories separate.

## Reproduce CI exactly (Docker, zero local install)

To debug a CI failure with byte-for-byte CI parity and **no local install**,
build inside the same toolchain image the CI workflows use:

```
ghcr.io/nrfconnect/sdk-nrf-toolchain:v3.3.0
```

Start a shell in the image with the repo mounted and run the CI steps. Two
image quirks need handling: the entrypoint is `bash -c`, so pass
`--entrypoint /bin/bash` to run a script with `-lc`; and the mounted volume is
owned by the host, so set `git config --global --add safe.directory` before any
`git` or `west` call. The image ships no Rust and no `curl` (CI installs Rust
via an Action and `apt-get install curl`), so fetch the rustup installer with
`wget` and add the `thumbv8m.main-none-eabihf` target for the initiator. The
script also removes any pre-existing build directories — caches from a native
build reference host paths the container cannot see. Then run the same two
`ci/build.sh` invocations as CI:

```bash
docker run --rm -it --entrypoint /bin/bash -v "$PWD":/work -w /work \
  ghcr.io/nrfconnect/sdk-nrf-toolchain:v3.3.0 -lc '
    git config --global --add safe.directory /work
    west init -l . --mf west.yml
    west update -o=--depth=1 -n
    # remove any pre-existing build dirs: the container configures against its
    # own NCS checkout and cannot reuse build caches that reference host paths
    rm -rf initiator/build_* reflector/build_*
    # initiator only: the image ships no Rust and no curl — fetch the rustup
    # installer with wget, then add the ARM target
    wget -qO- https://sh.rustup.rs | sh -s -- -y
    source "$HOME/.cargo/env"
    rustup target add thumbv8m.main-none-eabihf
    NCS_DIR=$(pwd) bash ci/build.sh --target initiator --preset nrf54l15dk_cent_a1_1
    NCS_DIR=$(pwd) bash ci/build.sh --target reflector --preset nrf54l15dk_peri_a1_4
  '
```

Both paths are CI-verified — the native path by `onboarding-verification` (a
clean `ubuntu:24.04` container), the Docker path by the `build-initiator` /
`build-reflector` and `release` jobs. The Docker image is the secondary path;
for day-to-day work and `west flash`, prefer the native install above.

## Out of scope

- A generated documentation site (Sphinx/MkDocs) — docs stay plain Markdown.
- The `nrfutil toolchain-manager` native path (broken on Python 3.12).
- Flashing and reading the COBS UART — see
  [docs/flash-quickstart.md](flash-quickstart.md).
- The preset, board, and antenna/path reference — see
  [docs/hardware.md](hardware.md).
- The firmware architecture and ranging-data flow — see
  [docs/architecture.md](architecture.md).
- The commit convention and pre-commit hooks — see
  [docs/contributing.md](contributing.md).

## References

- [README](../README.md) — project overview and firmware variants.
- [docs/flash-quickstart.md](flash-quickstart.md) — flashing prebuilt firmware
  and reading the COBS ranging output.
- [docs/hardware.md](hardware.md#presets) — boards, UARTs, and the preset table.
- [docs/architecture.md](architecture.md) — module map and ranging-data flow.
- [docs/contributing.md](contributing.md) — commit convention, hooks, and PR flow.
- [scripts/install-toolchain.sh](../scripts/install-toolchain.sh) — the install codifier.
- [ci/build.sh](../ci/build.sh) and [ci/common.sh](../ci/common.sh) — the build wrapper.
- [.github/workflows/ci.yml](../.github/workflows/ci.yml) — `onboarding-verification`
  (native) and `build-initiator` / `build-reflector` (Docker).
- [.github/workflows/release.yml](../.github/workflows/release.yml) — the
  multi-preset release build (Docker).
- [west.yml](../west.yml) — pins NCS v3.3.0 (`nrf` / sdk-nrf @ v3.3.0, `import: true`).
- [nRF Connect SDK v3.3.0](https://developer.nordicsemi.com/nRF_Connect_SDK/)
  and Zephyr SDK 0.17.4.
