#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

find_repo_root() {
  local script_dir
  script_dir="$(cd "$(dirname "$0")" && pwd)"
  if command -v git >/dev/null 2>&1; then
    git -C "${script_dir}" rev-parse --show-toplevel
  else
    cd "${script_dir}/.." && pwd
  fi
}

find_ncs_dir() {
  if [[ -n "${NCS_DIR:-}" ]] && [[ -d "${NCS_DIR}" ]]; then
    echo "${NCS_DIR}"
    return 0
  fi
  local candidate="/opt/nordic/ncs/current"
  if [[ -d "${candidate}" ]]; then
    echo "${candidate}"
    return 0
  fi
  echo "NCS_DIR not found. Set NCS_DIR or ensure /opt/nordic/ncs/current exists." >&2
  return 1
}

parse_preset() {
  local target_dir="$1"
  local preset_name="$2"
  local repo_root="${3:-$(find_repo_root)}"
  local app_dir="${repo_root}/${target_dir}"
  local presets_file="${app_dir}/CMakePresets.json"

  if [[ ! -f "${presets_file}" ]]; then
    echo "Error: CMakePresets.json not found at ${presets_file}" >&2
    return 1
  fi

  local preset_json
  preset_json="$(python3 -c "
import json, sys
with open('${presets_file}') as f:
    data = json.load(f)
for p in data['configurePresets']:
    if p['name'] == '${preset_name}':
        json.dump(p, sys.stdout)
        break
else:
    sys.exit(1)
")"
  if [[ $? -ne 0 ]]; then
    echo "Error: preset '${preset_name}' not found in ${presets_file}" >&2
    return 1
  fi

  local board conf extra_conf overlay binary_dir
  board="$(echo "${preset_json}" | python3 -c "import json,sys; print(json.load(sys.stdin)['cacheVariables']['BOARD'])")"
  conf="$(echo "${preset_json}" | python3 -c "import json,sys; print(json.load(sys.stdin)['cacheVariables']['CONF_FILE'])")"
  extra_conf="$(echo "${preset_json}" | python3 -c "
import json, sys
data = json.load(sys.stdin)['cacheVariables'].get('EXTRA_CONF_FILE', '')
if not data:
    sys.exit(0)
print(data)
")"
  overlay="$(echo "${preset_json}" | python3 -c "
import json, sys
data = json.load(sys.stdin)['cacheVariables'].get('DTC_OVERLAY_FILE', '')
if not data:
    sys.exit(0)
print(data)
")"
  binary_dir="$(echo "${preset_json}" | python3 -c "
import json, sys
bd = json.load(sys.stdin).get('binaryDir', '')
if not bd:
    sys.exit(0)
import os
bd = bd.replace('\${sourceDir}', '${app_dir}')
print(os.path.normpath(bd))
")"

  local resolved_conf="${app_dir}/${conf}"

  local resolved_extra_conf=""
  if [[ -n "${extra_conf}" ]]; then
    local IFS_old="${IFS}"
    IFS=';'
    local parts=()
    read -ra parts <<< "${extra_conf}"
    IFS="${IFS_old}"
    resolved=""
    for p in "${parts[@]}"; do
      resolved="${resolved:+${resolved};}${app_dir}/${p}"
    done
    resolved_extra_conf="${resolved}"
  fi

  local resolved_overlay=""
  if [[ -n "${overlay}" ]]; then
    resolved_overlay="${app_dir}/${overlay}"
  fi

  echo "BOARD=${board}"
  echo "CONF_FILE=${resolved_conf}"
  echo "EXTRA_CONF_FILE=${resolved_extra_conf}"
  echo "DTC_OVERLAY_FILE=${resolved_overlay}"
  echo "BINARY_DIR=${binary_dir}"
}