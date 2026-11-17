#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Shared version parsing for check-ncs-version.sh and bump-ncs.sh.
# Source this library from Bash; it does not change the caller's shell options.
# Requires Git, Python 3, and standard shell utilities.
# west.yml's nrf revision is the reference for current NCS version literals.

# Print the current Git repository's root directory.
find_repo_root() {
  git rev-parse --show-toplevel
}

# Print the nrf revision from an optional manifest path (default: repo west.yml).
# Return nonzero if the file is missing or no vMAJOR.MINOR.PATCH revision is found.
ncs_version_from_west() {
  local west="${1:-$(find_repo_root)/west.yml}"
  [[ -f "$west" ]] || { echo "Error: west.yml not found at ${west}" >&2; return 1; }
  local ver
  ver="$(python3 - "$west" <<'PY'
import re, sys, pathlib
text = pathlib.Path(sys.argv[1]).read_text()
# Match the first revision after a standalone "- name: nrf" line.
# This regex assumes the manifest layout; it does not parse YAML project boundaries.
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

# Case-insensitive grep ERE for v-prefixed versions in toolchain image tags,
# NCS/SDK prose, and workspace/venv cache keys. Other spellings and variable
# expressions are not detected; extend this pattern when introducing a new form.
NCS_REF_PATTERN='sdk-nrf-toolchain:v[0-9]+\.[0-9]+\.[0-9]+|nrf[[:space:]]+connect[[:space:]]+sdk[[:space:]]+v[0-9]+\.[0-9]+\.[0-9]+|ncs[[:space:]]+v[0-9]+\.[0-9]+\.[0-9]+|sdk-nrf[[:space:]]+@[[:space:]]+v[0-9]+\.[0-9]+\.[0-9]+|ncs-ws-v[0-9]+\.[0-9]+\.[0-9]+|ncs-venv-v[0-9]+\.[0-9]+\.[0-9]+'

# Emit file:line:version for recognized references in tracked working-tree files.
# Exclude the manifest and historical release records. No matches is not an error.
find_ncs_refs() {
  local root f lineno match ver
  root="$(find_repo_root)"
  while IFS= read -r f; do
    [[ "$f" == "west.yml" ]] && continue
    # Preserve version references in historical release records.
    [[ "$f" == "CHANGELOG.md" ]] && continue
    [[ "$f" == .github/release-notes/* ]] && continue
    [[ -f "${root}/${f}" ]] || continue
    while IFS= read -r line; do
      # Split only the first colon; image-tag matches also contain colons.
      lineno="${line%%:*}"
      match="${line#*:}"
      [[ -z "$match" ]] && continue
      ver="$(printf '%s' "$match" | grep -oE 'v[0-9]+\.[0-9]+\.[0-9]+' | head -n1)" || true
      [[ -n "$ver" ]] && printf '%s:%s:%s\n' "$f" "$lineno" "$ver"
    done < <(grep -noEi "$NCS_REF_PATTERN" "${root}/${f}" 2>/dev/null || true)
  done < <(cd "$root" && git ls-files || true)
}
