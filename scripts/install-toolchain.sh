#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Install the native Debian/Ubuntu build environment: host packages, a Python
# venv, the NCS workspace, and the ARM Zephyr SDK. Add --with-rust for initiators.
# Requires internet access and root/sudo when system packages are missing.
# SDK sources are placed beside the repository; the Python venv lives in the repository.
# Reruns reuse existing installations but still update workspace and Python dependencies.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# --- defaults (overridable via environment) ---
REPO_ROOT="${REPO_ROOT:-$(cd "${SCRIPT_DIR}/.." && pwd)}"
VENV_DIR="${VENV_DIR:-}"
WITH_RUST=0

usage() {
  cat <<'EOF'
Usage: install-toolchain.sh [--with-rust] [options]

Install the Debian/Ubuntu build environment for this repository.
Requires internet access and root/sudo for missing system packages.

Options:
  --with-rust        Install Rust and its ARM target if needed for the initiator.
  --repo-root PATH   Manifest repository (default: this script's repository).
  --venv-dir PATH    Python environment (default: <repo-root>/.venv).
  -h, --help         Show this help.

Environment overrides: REPO_ROOT, VENV_DIR; command-line options take precedence.
SDK sources are downloaded into the repository's parent directory.
The Zephyr SDK defaults to $HOME/zephyr-sdk-<version>; Rust uses rustup defaults.
Reruns reuse installed tools and refresh workspace and Python dependencies.
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

VENV_DIR="${VENV_DIR:-${REPO_ROOT}/.venv}"

[[ -d "${REPO_ROOT}" ]] || { echo "Error: repo root not found: ${REPO_ROOT}" >&2; exit 1; }
[[ -f "${REPO_ROOT}/west.yml" ]] || { echo "Error: west.yml not found in ${REPO_ROOT}" >&2; exit 1; }

log() { echo "[install-toolchain] $*"; }

# --- 1. host system packages ---
install_host_packages() {
  local packages=(
    build-essential cmake gperf wget ninja-build device-tree-compiler dfu-util
    git python3 python3-venv curl ca-certificates
  )

  # Avoid apt and sudo when all required packages are installed.
  local missing=()
  for pkg in "${packages[@]}"; do
    # A known but removed package is not an installed dependency.
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
  # NCS depends on spsdk, which requires setuptools below version 81.
  pip install --upgrade pip wheel "setuptools<81" west
}

# --- 3. west workspace ---
init_workspace() {
  cd "${REPO_ROOT}"
  # Refuse an existing workspace whose manifest belongs to another project.
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
  # Use the workspace SDK version and apt-provided host tools.
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
  # Reuse an installed ARM target.
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
