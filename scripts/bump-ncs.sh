#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Update the pinned NCS version and matching references, then validate them.
# Normal runs also update GitHub's NCS_VERSION variable, commit all working-tree
# changes on a new branch, push it, and request a pull request.
# --dry-run still edits local files but skips those GitHub and Git operations.
# Use a clean working tree: validation failure restores affected tracked files.
# Requires Bash, Git, GNU sed, and the version-checker's dependencies;
# normal runs also require authenticated gh with repository-variable access.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/ncs-version-lib.sh"

REPO_ROOT="$(find_repo_root)"
DRY_RUN=0

usage() {
  cat <<'EOF'
Usage: bump-ncs.sh [options] <version>

Update west.yml and matching NCS references, then run the version check.
By default, also update GitHub's NCS_VERSION variable, create a branch,
commit all working-tree changes, push, and open a pull request.

  <version>      Target version in vMAJOR.MINOR.PATCH format.
  --dry-run      Edit and validate local files only; review with git diff.
  -h, --help     Show this help.

Start from a clean working tree. --dry-run modifies files too.
If validation fails, affected tracked files are restored from Git.
Normal runs require authenticated gh with repository-variable access.
EOF
}

POSITIONAL_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run)  DRY_RUN=1; shift ;;
    -h|--help)  usage; exit 0 ;;
    -*) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
    *)  POSITIONAL_ARGS+=("$1"); shift ;;
  esac
done

# Accept options before or after the version argument.
if [[ ${#POSITIONAL_ARGS[@]} -gt 1 ]]; then
  echo "Error: unexpected argument: ${POSITIONAL_ARGS[1]}" >&2
  usage >&2
  exit 1
fi

NEW_VERSION="${POSITIONAL_ARGS[0]:-}"
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

# Escape version dots for literal matching in sed.
OLD_SEMVER="${OLD_VERSION#v}"
NEW_SEMVER="${NEW_VERSION#v}"
OLD_SEMVER_ESC="${OLD_SEMVER//./\\.}"

log "Bumping NCS ${OLD_VERSION} -> ${NEW_VERSION}"

# Update files selected by the shared scanner, plus the manifest.
mapfile -t FILES < <(find_ncs_refs | cut -d: -f1 | sort -u)
FILES+=("west.yml")

# Replace every occurrence of the old v-prefixed version in each selected file.
for f in "${FILES[@]}"; do
  path="${REPO_ROOT}/${f}"
  [[ -f "$path" ]] || continue
  if grep -qF "v${OLD_SEMVER}" "$path"; then
    sed -i "s/v${OLD_SEMVER_ESC}/v${NEW_SEMVER}/g" "$path"
    log "updated ${f}"
  fi
done

# Restore affected tracked files if the rewritten versions fail validation.
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

# Keep the CI repository variable aligned with the manifest.
log "setting vars.NCS_VERSION=${NEW_VERSION}"
gh variable set NCS_VERSION --body "$NEW_VERSION"

# Commit and open a PR.
BRANCH="chore/bump-ncs-${NEW_SEMVER}"
log "creating branch ${BRANCH}"
git -C "$REPO_ROOT" checkout -b "$BRANCH"
git -C "$REPO_ROOT" add -A
git -C "$REPO_ROOT" commit -m "chore: bump NCS ${OLD_VERSION} -> ${NEW_VERSION}

Updates the manifest and matching CI, documentation, and script references."
git -C "$REPO_ROOT" push -u origin "$BRANCH"
gh pr create --title "chore: bump NCS ${OLD_VERSION} -> ${NEW_VERSION}" \
  --body "Bumps NCS ${OLD_VERSION} -> ${NEW_VERSION} via \`scripts/bump-ncs.sh\`.

- west.yml \`nrf\` revision -> ${NEW_VERSION}
- README/docs/script literals -> ${NEW_VERSION}
- \`vars.NCS_VERSION\` repo variable -> ${NEW_VERSION}" || true
log "done: PR opened on ${BRANCH}"
