#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Install codifier for the native NCS toolchain used by mars-cs-nrf54l.
#
# One invocation hides the entire native install behind a single command:
#   host apt packages -> isolated Python venv -> west workspace ->
#   Zephyr/NCS pip requirements -> Zephyr SDK (ARM-only; version from SDK_VERSION).
#
# Idempotent: every stage guards on existing state, so re-running on an already
# installed venv / west workspace / SDK, or resuming after a partial failure,
# completes without re-downloading or breaking state.
#
# The reflector build does not need Rust; pass --with-rust to also install the
# thumbv8m.main-none-eabihf target required by the initiator.
#
# The nrfutil toolchain-manager path is intentionally NOT used: the pip-distributed
# legacy nrfutil is broken on Python 3.12.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# --- defaults (overridable via environment) ---
REPO_ROOT="${REPO_ROOT:-$(cd "${SCRIPT_DIR}/.." && pwd)}"
VENV_DIR="${VENV_DIR:-${HOME}/.ncs-venv}"
WITH_RUST=0

usage() {
  cat <<'EOF'
Usage: install-toolchain.sh [--with-rust] [options]

Installs the native NCS toolchain (host packages, isolated venv, west
workspace, Zephyr SDK ARM-only; version derived from SDK_VERSION) behind a
single idempotent invocation.

Options:
  --with-rust        Also install the thumbv8m.main-none-eabihf Rust target
                     (required by the initiator; the reflector does not need it).
  --repo-root PATH   Manifest repo to run `west init -l` from
                     (default: parent of this script's directory).
  --venv-dir PATH    Isolated venv location (default: $HOME/.ncs-venv).
  -h, --help         Show this help.

Environment overrides: REPO_ROOT, VENV_DIR.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --with-rust)    WITH_RUST=1; shift ;;
    --repo-root)    REPO_ROOT="${2:-}"; shift 2 ;;
    --venv-dir)     VENV_DIR="${2:-}"; shift 2 ;;
    -h|--help)      usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

[[ -d "${REPO_ROOT}" ]] || { echo "Error: repo root not found: ${REPO_ROOT}" >&2; exit 1; }
[[ -f "${REPO_ROOT}/west.yml" ]] || { echo "Error: west.yml not found in ${REPO_ROOT}" >&2; exit 1; }

log() { echo "[install-toolchain] $*"; }

# --- 1. host system packages ---
install_host_packages() {
  local packages=(
    build-essential cmake gperf wget ninja-build device-tree-compiler dfu-util
    git python3 python3-venv curl ca-certificates
  )

  # Fast path: if every required package is already installed, there is nothing
  # to do and no root is needed. Lets the script re-run idempotently on a host
  # that is already provisioned without forcing sudo.
  local missing=()
  for pkg in "${packages[@]}"; do
    # `dpkg -s` exits 0 for any package known to dpkg, including ones in `rc`
    # (removed-but-not-purged) state whose binaries are absent. Require the full
    # "install ok installed" status so a half-removed package is reinstalled.
    if ! dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q '^install ok installed$'; then
      missing+=("$pkg")
    fi
  done
  if [[ ${#missing[@]} -eq 0 ]]; then
    log "Host apt packages already installed, skipping"
    return 0
  fi

  log "Installing missing host apt packages: ${missing[*]}"
  if [[ "$(id -u)" -ne 0 ]] && ! command -v sudo >/dev/null 2>&1; then
    echo "Error: need root (or sudo) to install apt packages: ${missing[*]}" >&2
    return 1
  fi
  local sudo_cmd=""
  [[ "$(id -u)" -ne 0 ]] && sudo_cmd="sudo"
  ${sudo_cmd} apt-get update
  ${sudo_cmd} apt-get install -y --no-install-recommends "${packages[@]}"
}

# --- 2. isolated Python venv ---
create_venv() {
  log "Preparing isolated venv at ${VENV_DIR}"
  if [[ ! -d "${VENV_DIR}" ]]; then
    python3 -m venv "${VENV_DIR}"
  else
    log "venv already exists, reusing"
  fi
  # venv activate scripts may reference unset variables; relax nounset briefly.
  # shellcheck disable=SC1091
  set +u
  source "${VENV_DIR}/bin/activate"
  set -u
  # Pin setuptools<81: the NCS requirements (installed later) pull spsdk, which
  # requires setuptools<81. A blanket --upgrade would pull the latest setuptools
  # (>=81) and break that constraint. <81 is the latest spsdk-compatible major;
  # bump it when the NCS/spsdk setuptools bound moves.
  pip install --upgrade pip wheel "setuptools<81" west
}

# --- 3. west workspace ---
init_workspace() {
  cd "${REPO_ROOT}"
  # `west manifest --path` prints the active workspace's manifest file and fails
  # (non-zero) when no `.west` ancestor exists. Comparing it to
  # ${REPO_ROOT}/west.yml is a one-shot test that THIS repo is the manifest
  # project of the surrounding workspace — robust to repo renaming because it
  # uses absolute paths, not basename matching. A foreign ancestor workspace
  # (manifest points elsewhere) is refused rather than silently bound, which
  # would otherwise make `west update` clone the wrong manifest's projects.
  local manifest_path
  if manifest_path="$(west manifest --path 2>/dev/null)"; then
    if [[ "${manifest_path}" == "${REPO_ROOT}/west.yml" ]]; then
      log "west workspace already initialized (manifest: ${manifest_path}), skipping west init"
    else
      echo "Error: refusing to bind to a foreign west workspace." >&2
      echo "  this repo:    ${REPO_ROOT}" >&2
      echo "  its manifest: ${manifest_path}" >&2
      return 1
    fi
  else
    log "Initializing west workspace from ${REPO_ROOT}/west.yml"
    west init -l . --mf west.yml
  fi
}

# --- 4. west update (resumable, shallow) ---
update_workspace() {
  log "Running west update (shallow, resumable)"
  west update -o=--depth=1 -n
}

# --- 5. Zephyr/NCS python requirements (from the cloned workspace) ---
install_python_reqs() {
  local ws_root
  ws_root="$(west topdir)"
  log "Installing Zephyr/NCS pip requirements from ${ws_root}"
  pip install -r "${ws_root}/zephyr/scripts/requirements.txt"
  pip install -r "${ws_root}/nrf/scripts/requirements.txt"
}

# --- 6. Zephyr SDK, ARM-only (no bundled host tools) ---
install_sdk() {
  local ws_root
  ws_root="$(west topdir)"
  local sdk_version_file="${ws_root}/zephyr/SDK_VERSION"
  if [[ ! -f "${sdk_version_file}" ]]; then
    echo "Error: ${sdk_version_file} not found; run 'west update' first." >&2
    return 1
  fi
  local sdk_version
  sdk_version="$(<"${sdk_version_file}")"
  local sdk_dir="${HOME}/zephyr-sdk-${sdk_version}"
  local gcc="${sdk_dir}/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc"
  if [[ -x "${gcc}" ]]; then
    log "Zephyr SDK ${sdk_version} toolchain already installed at ${sdk_dir}, skipping"
    return 0
  fi
  log "Installing Zephyr SDK ${sdk_version} (ARM-only, no host tools) via west sdk install"
  # west sdk install auto-detects the version from ${ZEPHYR_BASE}/SDK_VERSION
  # (read above into ${sdk_version}) and installs to the default
  # ${HOME}/zephyr-sdk-<version> path, which Zephyr auto-discovers — so no -d.
  #
  # -H / --no-hosttools: skip the SDK's bundled host tools (cmake/ninja/gperf/
  # dtc). This repo installs its own host tools via apt, and the SDK's
  # host-tools install fails on minimal containers (e.g. ubuntu:24.04) that
  # lack the shared libraries the bundled host-tool binaries require. The build
  # uses system host tools from PATH and only needs the arm-zephyr-eabi
  # toolchain, which Zephyr auto-discovers at its default install path.
  west sdk install -t arm-zephyr-eabi -H
}

# --- 7. (optional) Rust target for the initiator ---
install_rust() {
  log "Installing Rust target thumbv8m.main-none-eabihf"
  if ! command -v rustup >/dev/null 2>&1; then
    log "rustup not found, installing rustup"
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
    # shellcheck disable=SC1091
    source "${HOME}/.cargo/env"
  fi
  # Idempotent fast path: `rustup target add` tolerates an already-added
  # target, but guarding explicitly matches the script's per-stage idempotency
  # contract and avoids re-touching the toolchain on every re-run.
  if rustup target list --installed | grep -qx 'thumbv8m.main-none-eabihf'; then
    log "Rust target thumbv8m.main-none-eabihf already installed, skipping"
    return 0
  fi
  rustup target add thumbv8m.main-none-eabihf
}

main() {
  install_host_packages
  create_venv
  init_workspace
  update_workspace
  install_python_reqs
  install_sdk
  [[ "${WITH_RUST}" -eq 1 ]] && install_rust

  log "Done. To build, activate the venv and run:"
  log "  source ${VENV_DIR}/bin/activate"
  log "  bash ci/build.sh --target reflector --preset nrf54l15dk_peri_a1_4"
}

main
