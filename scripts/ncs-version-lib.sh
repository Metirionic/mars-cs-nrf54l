#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Shared NCS-version helpers for mars-cs-nrf54l.
#
# Sourced by scripts/check-ncs-version.sh (the guard) and scripts/bump-ncs.sh
# (the bump operation) so the two agree on exactly what counts as an NCS
# version reference. Mirrors the ci/common.sh convention: a functions-only
# library, no `set` flags (callers set their own).
#
# Canonical source of truth: west.yml's `nrf` project `revision`. Every other
# NCS version literal in the repo (container image tags, cache keys, README/docs
# prose, script comments) is expected to agree with it.

# find_repo_root -> repo top-level path (matches scripts/check-docs.sh).
find_repo_root() {
  git rev-parse --show-toplevel
}

# ncs_version_from_west -> print the `nrf` project revision (vX.Y.Z) from
# west.yml. Fails loudly if west.yml is missing or the revision can't be
# parsed. Uses a python3 line-state regex (no PyYAML dependency; ci/common.sh
# already relies on python3 for parsing).
ncs_version_from_west() {
  local west="${1:-$(find_repo_root)/west.yml}"
  [[ -f "$west" ]] || { echo "Error: west.yml not found at ${west}" >&2; return 1; }
  local ver
  ver="$(python3 - "$west" <<'PY'
import re, sys, pathlib
text = pathlib.Path(sys.argv[1]).read_text()
# The `nrf` project block: `- name: nrf` then the first `revision: v<semver>`
# that follows it. Non-greedy + DOTALL stops at that first revision line, so
# a sibling project's revision can't be misread.
m = re.search(
    r'(?m)^\s*-\s+name:\s*nrf\s*$.*?^\s*revision:\s*(v[0-9]+\.[0-9]+\.[0-9]+)',
    text, re.DOTALL,
)
if not m:
    sys.exit(1)
print(m.group(1))
PY
)" || { echo "Error: could not parse nrf revision from ${west}" >&2; return 1; }
  printf '%s\n' "$ver"
}

# NCS_REF_PATTERN — grep extended-regex alternation (use with -i) covering every
# phrasing of an in-repo NCS version literal, each ending in the v<semver> tail:
#   sdk-nrf-toolchain:v<semver>   container image tag (CI + doc docker-run)
#   nrf connect sdk v<semver>     README/docs prose link text
#   ncs v<semver>                 docs prose
#   sdk-nrf @ v<semver>           docs prose
#   ncs-ws-v<semver>              west-workspace cache-key literal
#   ncs-venv-v<semver>            NCS venv cache-key literal
# The cache-key forms are included so a future re-introduced *literal* cache
# key is caught; the ${{ vars.NCS_VERSION }} expression form does not match
# (no `v<digit>` after the prefix). Excludes CHANGELOG app versions and
# pre-commit hook revs, which don't match any of these phrasings.
NCS_REF_PATTERN='sdk-nrf-toolchain:v[0-9]+\.[0-9]+\.[0-9]+|nrf[[:space:]]+connect[[:space:]]+sdk[[:space:]]+v[0-9]+\.[0-9]+\.[0-9]+|ncs[[:space:]]+v[0-9]+\.[0-9]+\.[0-9]+|sdk-nrf[[:space:]]+@[[:space:]]+v[0-9]+\.[0-9]+\.[0-9]+|ncs-ws-v[0-9]+\.[0-9]+\.[0-9]+|ncs-venv-v[0-9]+\.[0-9]+\.[0-9]+'

# find_ncs_refs -> emit `file:lineno:version` for every NCS version literal in
# tracked files, excluding west.yml (the canonical source, allowlisted). The
# version is the v<semver> tail of each match. Returns 0 even with no matches
# so callers under `set -e` don't abort.
find_ncs_refs() {
  local root f lineno match ver
  root="$(find_repo_root)"
  while IFS= read -r f; do
    [[ "$f" == "west.yml" ]] && continue
    [[ -f "${root}/${f}" ]] || continue
    while IFS= read -r line; do
      # grep -no output is "lineno:match"; the match may itself contain ':'
      # (e.g. sdk-nrf-toolchain:v3.4.0), so split only the first field.
      lineno="${line%%:*}"
      match="${line#*:}"
      [[ -z "$match" ]] && continue
      ver="$(printf '%s' "$match" | grep -oE 'v[0-9]+\.[0-9]+\.[0-9]+' | head -n1)" || true
      [[ -n "$ver" ]] && printf '%s:%s:%s\n' "$f" "$lineno" "$ver"
    done < <(grep -noEi "$NCS_REF_PATTERN" "${root}/${f}" 2>/dev/null || true)
  done < <(cd "$root" && git ls-files || true)
}
