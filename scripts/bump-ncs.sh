#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Bump the NCS version across the repo in one operation.
#
# The single way to move the NCS version: rewrites west.yml's `nrf` revision
# (the canonical source) and every NCS version literal the shared scanner finds
# (README/docs/scripts), sets the `vars.NCS_VERSION` GitHub repo variable to
# match, and opens a PR. scripts/check-ncs-version.sh is the backstop that
# catches anything this script missed.
#
# Docs "update automatically when the SDK is bumped" by construction: this
# script IS the bump.
#
# `--dry-run` rewrites the files and runs the self-check but skips
# `gh variable set` and PR creation, so it is safe to run locally for testing.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/ncs-version-lib.sh"

REPO_ROOT="$(find_repo_root)"
DRY_RUN=0

usage() {
  cat <<'EOF'
Usage: bump-ncs.sh <version> [options]

Bump the NCS version across the repo: west.yml's `nrf` revision, every NCS
literal the scanner finds (README/docs/scripts), the `vars.NCS_VERSION` GitHub
repo variable, and open a PR.

  <version>      Target NCS version, e.g. v3.4.0 (must match vMAJOR.MINOR.PATCH).
  --dry-run      Rewrite files and run the self-check, but skip `gh variable
                 set` and PR creation. Safe for local testing; review with
                 `git diff` and revert with `git checkout -- .`.
  -h, --help     Show this help.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run)  DRY_RUN=1; shift ;;
    -h|--help)  usage; exit 0 ;;
    -*) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
    *)  break ;;
  esac
done

NEW_VERSION="${1:-}"
if [[ -z "$NEW_VERSION" ]]; then
  echo "Error: <version> is required" >&2
  usage >&2
  exit 1
fi
if ! [[ "$NEW_VERSION" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Error: <version> must match vMAJOR.MINOR.PATCH (got: ${NEW_VERSION})" >&2
  exit 1
fi

log() { echo "[bump-ncs] $*"; }

OLD_VERSION="$(ncs_version_from_west)"
if [[ "$NEW_VERSION" == "$OLD_VERSION" ]]; then
  log "west.yml already at ${OLD_VERSION}; nothing to do"
  exit 0
fi

# Escape the semver dots for use as a sed/grep pattern (otherwise '.' matches
# any char). Fixed-string grep (-F) needs no escaping, but the sed substitution
# does.
OLD_SEMVER="${OLD_VERSION#v}"
NEW_SEMVER="${NEW_VERSION#v}"
OLD_SEMVER_ESC="${OLD_SEMVER//./\\.}"

log "Bumping NCS ${OLD_VERSION} -> ${NEW_VERSION}"

# Collect the unique files that currently contain an NCS literal, plus
# west.yml (the source, always rewritten).
mapfile -t FILES < <(find_ncs_refs | cut -d: -f1 | sort -u)
FILES+=("west.yml")

# Rewrite the old semver to the new in every target file. Safe because the
# scanner confirmed these files' v<semver> tokens are NCS references, and the
# only other semvers in these files (e.g. Zephyr SDK 0.17.4) lack the v prefix.
for f in "${FILES[@]}"; do
  path="${REPO_ROOT}/${f}"
  [[ -f "$path" ]] || continue
  if grep -qF "v${OLD_SEMVER}" "$path"; then
    sed -i "s/v${OLD_SEMVER_ESC}/v${NEW_SEMVER}/g" "$path"
    log "updated ${f}"
  fi
done

# Self-check: the guard must pass against the new west.yml. On failure, revert
# the working-tree edits so a half-bump doesn't linger.
log "running guard self-check"
if ! bash "${SCRIPT_DIR}/check-ncs-version.sh"; then
  echo "Error: guard failed after rewrite; some literal was missed." >&2
  git -C "$REPO_ROOT" checkout -- "${FILES[@]}" 2>/dev/null || true
  exit 1
fi

if [[ "$DRY_RUN" -eq 1 ]]; then
  log "dry-run: skipping gh variable set and PR creation"
  log "done (dry-run). Review with: git -C ${REPO_ROOT} diff"
  exit 0
fi

# Set the GitHub repo variable so CI's vars.NCS_VERSION tracks the manifest.
log "setting vars.NCS_VERSION=${NEW_VERSION}"
gh variable set NCS_VERSION --body "$NEW_VERSION"

# Commit and open a PR.
BRANCH="chore/bump-ncs-${NEW_SEMVER}"
log "creating branch ${BRANCH}"
git -C "$REPO_ROOT" checkout -b "$BRANCH"
git -C "$REPO_ROOT" add -A
git -C "$REPO_ROOT" commit -m "chore: bump NCS ${OLD_VERSION} -> ${NEW_VERSION}

Bumps the NCS version across west.yml, CI, docs, and scripts via the single
bump operation (scripts/bump-ncs.sh).

Co-Authored-By: Claude <noreply@anthropic.com>"
git -C "$REPO_ROOT" push -u origin "$BRANCH"
gh pr create --title "chore: bump NCS ${OLD_VERSION} -> ${NEW_VERSION}" \
  --body "Bumps NCS ${OLD_VERSION} -> ${NEW_VERSION} via \`scripts/bump-ncs.sh\`.

- west.yml \`nrf\` revision -> ${NEW_VERSION}
- README/docs/script literals -> ${NEW_VERSION}
- \`vars.NCS_VERSION\` repo variable -> ${NEW_VERSION}

Refs #19." || true
log "done: PR opened on ${BRANCH}"
