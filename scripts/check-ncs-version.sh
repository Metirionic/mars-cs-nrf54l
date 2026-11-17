#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Check recognized NCS references against west.yml's nrf revision.
# Run: bash scripts/check-ncs-version.sh
# Requires Bash and the dependencies of ncs-version-lib.sh.
#
# In GitHub Actions, also require NCS_VERSION to match unless
# ENFORCE_NCS_VAR=false (used for pull requests). Local runs check files only.
# Write ncs_version to GITHUB_OUTPUT and a report to GITHUB_STEP_SUMMARY when set.
# Exit nonzero on a version mismatch or an unreadable manifest.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/ncs-version-lib.sh"

cd "$(find_repo_root)"

errors=0
summary_lines=()

# Record an error for stderr and the optional CI summary.
add_error() {
  local msg="$1"
  printf '  - %s\n' "$msg" >&2
  summary_lines+=("$msg")
  errors=$((errors + 1))
}

expected="$(ncs_version_from_west)" || {
  add_error "could not parse nrf revision from west.yml (is the manifest well-formed?)"
  exit 1
}

# Publish the manifest version for downstream build jobs.
if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  printf 'ncs_version=%s\n' "$expected" >> "$GITHUB_OUTPUT"
fi

# Workflows supply the repository variable as NCS_VERSION. Pull-request jobs
# disable this check so SDK updates can be reviewed before the variable changes.
if [[ "${GITHUB_ACTIONS:-}" == "true" && "${ENFORCE_NCS_VAR:-true}" == "true" ]]; then
  if [[ -z "${NCS_VERSION:-}" ]]; then
    add_error "NCS_VERSION repo variable is not set; run: gh variable set NCS_VERSION ${expected}"
  elif [[ "${NCS_VERSION}" != "${expected}" ]]; then
    add_error "vars.NCS_VERSION=${NCS_VERSION} differs from west.yml nrf revision=${expected}"
  fi
fi

# Check the references recognized by the shared scanner.
while IFS=: read -r file lineno version; do
  [[ -z "${version:-}" ]] && continue
  if [[ "$version" != "$expected" ]]; then
    add_error "stale NCS literal at ${file}:${lineno}: got ${version}, expected ${expected}"
  fi
done < <(find_ncs_refs)

# --- report -----------------------------------------------------------------

if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
  {
    printf '### NCS version check\n\n'
    if (( errors > 0 )); then
      printf '**Failed** — %d problem(s):\n\n' "$errors"
      printf '```\n'
      printf '  - %s\n' "${summary_lines[@]}"
      printf '```\n'
    else
      printf '**Passed** — all recognized NCS references agree with west.yml (nrf @ %s).\n' "$expected"
    fi
  } >> "$GITHUB_STEP_SUMMARY" 2>/dev/null || true
fi

if (( errors > 0 )); then
  printf 'ncs version check failed: %d problem(s) (see above)\n' "$errors" >&2
  exit 1
fi
printf 'ncs version check passed: all recognized NCS references agree with west.yml (nrf @ %s)\n' "$expected"
