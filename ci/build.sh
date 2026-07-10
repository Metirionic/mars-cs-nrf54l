#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

REPO_ROOT="$(find_repo_root)"

TARGET=""
PRESETS=""
PRISTINE="auto"
RELEASE_DIR=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --target)       TARGET="${2:-}"; shift 2 ;;
    --preset)       PRESETS="${2:-}"; shift 2 ;;
    --pristine)     PRISTINE="${2:-}"; shift 2 ;;
    --release-dir)  RELEASE_DIR="${2:-}"; shift 2 ;;
    -h|--help)      cat <<'EOF'
Usage: build.sh --target <path> --preset <name>[,name2,...] [options]

Required:
  --target PATH     Relative path from repo root to the Zephyr app directory
  --preset NAMES    Comma-separated CMake preset name(s)

Optional:
  --pristine POLICY West pristine policy (default: auto)
  --release-dir DIR Copy firmware artifacts to this directory after build
EOF
      exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 1 ;;
  esac
done

[[ -z "${TARGET}" ]]  && { echo "Error: --target is required." >&2; exit 1; }
[[ -z "${PRESETS}" ]] && { echo "Error: --preset is required." >&2; exit 1; }

TARGET_DIR="${REPO_ROOT}/${TARGET}"
[[ ! -d "${TARGET_DIR}" ]] && { echo "Error: target dir not found: ${TARGET_DIR}" >&2; exit 1; }

NCS_DIR="$(find_ncs_dir)"
APP_NAME="$(basename "${TARGET}")"

FAILED=0

IFS=',' read -ra PRESET_LIST <<< "${PRESETS}"
for PRESET in "${PRESET_LIST[@]}"; do
  echo "=== Building ${APP_NAME}/${PRESET} ==="

  PRESET_DATA="$(parse_preset "${TARGET}" "${PRESET}" "${REPO_ROOT}")"

  BOARD="$(echo "${PRESET_DATA}" | grep '^BOARD=' | cut -d= -f2-)"
  CONF_FILE="$(echo "${PRESET_DATA}" | grep '^CONF_FILE=' | cut -d= -f2-)"
  EXTRA_CONF_FILE="$(echo "${PRESET_DATA}" | grep '^EXTRA_CONF_FILE=' | cut -d= -f2-)"
  DTC_OVERLAY_FILE="$(echo "${PRESET_DATA}" | grep '^DTC_OVERLAY_FILE=' | cut -d= -f2-)"
  BUILD_DIR="$(echo "${PRESET_DATA}" | grep '^BINARY_DIR=' | cut -d= -f2-)"

  echo "    BOARD:       ${BOARD}"
  echo "    CONF_FILE:   ${CONF_FILE}"
  echo "    EXTRA_CONF:  ${EXTRA_CONF_FILE}"
  echo "    DTC_OVERLAY: ${DTC_OVERLAY_FILE}"
  echo "    BUILD_DIR:   ${BUILD_DIR}"

  cmake_args=(-DCONF_FILE="${CONF_FILE}")
  [[ -n "${EXTRA_CONF_FILE}" ]] && cmake_args+=(-DEXTRA_CONF_FILE="${EXTRA_CONF_FILE}")
  [[ -n "${DTC_OVERLAY_FILE}" ]] && cmake_args+=(-DDTC_OVERLAY_FILE="${DTC_OVERLAY_FILE}")

  pushd "${NCS_DIR}" > /dev/null
  if ! west build -p "${PRISTINE}" -d "${BUILD_DIR}" -b "${BOARD}" "${TARGET_DIR}" -- "${cmake_args[@]}"; then
    echo "Error: build failed for ${APP_NAME}/${PRESET}" >&2
    popd > /dev/null
    FAILED=1
    continue
  fi
  popd > /dev/null

  if [[ -n "${RELEASE_DIR}" ]]; then
    mkdir -p "${RELEASE_DIR}"

    hex_source="${BUILD_DIR}/merged.hex"
    if [[ ! -f "${hex_source}" ]]; then
      hex_source="${BUILD_DIR}/zephyr/zephyr.hex"
    fi
    if [[ -f "${hex_source}" ]]; then
      cp "${hex_source}" "${RELEASE_DIR}/${APP_NAME}_${PRESET}.hex"
    else
      echo "Error: no firmware hex found for ${APP_NAME}/${PRESET}" >&2
      FAILED=1
      continue
    fi
  fi

  echo "=== Finished ${APP_NAME}/${PRESET} ==="
done

if [[ "${FAILED}" -ne 0 ]]; then
  echo "" >&2
  echo "Some builds failed." >&2
  exit 1
fi

if [[ -n "${RELEASE_DIR}" ]]; then
  echo "" >&2
  echo "All builds succeeded. Firmware in ${RELEASE_DIR}/"
fi
