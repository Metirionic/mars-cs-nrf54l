#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# NCS version consistency guard for mars-cs-nrf54l.
#
# Asserts that every NCS version literal in the repo agrees with the canonical
# source (west.yml's `nrf` project revision), and — when run in CI — that the
# `vars.NCS_VERSION` GitHub repo variable agrees with it too. Catches stale
# literals (container image tags, cache keys, README/docs, script comments)
# before a bump leaves a half-built state or a tagless container image.
#
# In CI (.github/workflows/ci.yml `verify-ncs-version` job, and release.yml's
# build job) the workflow injects the repo variable via `env: NCS_VERSION:
# ${{ vars.NCS_VERSION }}`. Locally, run `bash scripts/check-ncs-version.sh`
# with no NCS_VERSION env: it just verifies every literal matches west.yml.
#
# Mirrors scripts/check-docs.sh: set -euo pipefail, an errors counter + summary
# list, a GITHUB_STEP_SUMMARY block, and a 1/0 exit with a pass/fail message.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/ncs-version-lib.sh"

cd "$(find_repo_root)"

errors=0
summary_lines=()

# add_error <message> — record one problem on stderr and in the CI summary.
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

# CI-only: the workflow injects vars.NCS_VERSION as NCS_VERSION. An empty value
# here would expand container.image to a tagless image and cache keys to
# 'ncs-ws--…' — an opaque failure far from the cause. Fail loudly with the
# remediation instead. Locally (GITHUB_ACTIONS unset) this block is skipped and
# west.yml is the reference.
if [[ "${GITHUB_ACTIONS:-}" == "true" ]]; then
  if [[ -z "${NCS_VERSION:-}" ]]; then
    add_error "NCS_VERSION repo variable is not set; run: gh variable set NCS_VERSION ${expected}"
  elif [[ "${NCS_VERSION}" != "${expected}" ]]; then
    add_error "vars.NCS_VERSION=${NCS_VERSION} differs from west.yml nrf revision=${expected}"
  fi
fi

# Every NCS literal in the repo (excluding west.yml, the source) must equal the
# canonical revision.
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
      printf '**Passed** — all NCS literals agree with west.yml (nrf @ %s).\n' "$expected"
    fi
  } >> "$GITHUB_STEP_SUMMARY" 2>/dev/null || true
fi

if (( errors > 0 )); then
  printf 'ncs version check failed: %d problem(s) (see above)\n' "$errors" >&2
  exit 1
fi
printf 'ncs version check passed: all NCS literals agree with west.yml (nrf @ %s)\n' "$expected"
