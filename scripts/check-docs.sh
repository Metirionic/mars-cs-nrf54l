#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Validate tracked README.md and docs/**/*.md guides:
# - Local inline links to existing files/directories and Markdown heading anchors.
# - Firmware archive naming across the flash guide and release workflow.
# - Hardware-table completeness against the CMake presets (prototypes excluded).
# - Release-list completeness and valid CI/build-guide preset selections.
#
# External URLs, reference-style links, and autolinks are not checked.
# Requires Bash, Git, Python 3, and standard shell utilities. No network access.
# Run: bash scripts/check-docs.sh
# Exits nonzero on errors; also writes GITHUB_STEP_SUMMARY when set.

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

errors=0
summary_lines=()

# Use GNU realpath when available; otherwise normalize paths in Bash.
if realpath -m --relative-to=. / >/dev/null 2>&1; then
  HAS_REALPATH=1
else
  HAS_REALPATH=0
fi


# Record an error for stderr and the optional GitHub Actions summary.
add_error() {
  local msg="$1"
  printf '  - %s\n' "$msg" >&2
  summary_lines+=("$msg")
  errors=$((errors + 1))
}

# --- link parsing -----------------------------------------------------------

# Emit line-numbered inline Markdown links. This is a regex parser, not a Markdown parser.
emit_links() {
  grep -noE '\[[^]]+\]\([^)]+\)' "$1" || true
}

# --- href resolution --------------------------------------------------------

# Normalize path segments without requiring GNU realpath; symlinks are not resolved.
normalize_relpath() {
  local p="$1" part idx
  local -a parts=() stack=()
  # Split and rejoin paths using a function-local separator.
  local IFS='/'
  read -r -a parts <<< "$p"
  for part in "${parts[@]}"; do
    case "$part" in
      ''|'.') ;;
      '..')
        # Preserve leading ".." segments when no component can be removed.
        idx=$((${#stack[@]}-1))
        if ((idx >= 0)) && [[ "${stack[idx]}" != '..' ]]; then
          unset "stack[idx]"
        else
          stack+=('..')
        fi ;;
      *)      stack+=("$part") ;;
    esac
  done
  local out="${stack[*]}"
  # Represent an empty normalized path as the current directory.
  [[ -n "$out" ]] || out='.'
  printf '%s\n' "$out"
}


# Resolve a destination relative to its document; set RESOLVED_TARGET and RESOLVED_ANCHOR.
resolve_href() {
  local linker="$1" href="$2" anchor="" base target
  if [[ "$href" == *'#'* ]]; then
    anchor="${href##*#}"
    href="${href%%#*}"
  fi
  if [[ -z "$href" ]]; then
    RESOLVED_TARGET="$linker"
    RESOLVED_ANCHOR="$anchor"
    return 0
  fi
  base="$(dirname "$linker")"
  if [[ "$HAS_REALPATH" == 1 ]]; then
    target="$(realpath -m --relative-to=. "${base}/${href}")"
  else
    target="$(normalize_relpath "${base}/${href}")"
  fi
  RESOLVED_TARGET="$target"
  RESOLVED_ANCHOR="$anchor"
}

# --- anchor validation ------------------------------------------------------

# Generate approximate GitHub anchors for ATX headings, preserving Unicode letters.
# Duplicate-heading suffixes and rendered inline formatting are not supported.
heading_slugs() {
  local file="$1"
  grep -nE '^#{1,6} ' "$file" 2>/dev/null \
    | sed -E 's/^[0-9]+:#*[[:space:]]+//' \
    | python3 -X utf8 -c '
import re
import sys
import unicodedata

for line in sys.stdin:
    # Normalize heading whitespace before removing punctuation.
    h = re.sub(r"\s+", " ", line.strip().lower())
    # Preserve Unicode letters, numbers, hyphens, and underscores.
    h = "".join(ch for ch in h if ch in "-_ "
                or unicodedata.category(ch)[0] == "L"
                or unicodedata.category(ch) in ("Nd", "Nl"))
    print(h.replace(" ", "-"))
' || true
}


# Return success when a Markdown heading matches the requested anchor.
anchor_exists() {
  local file="$1" anchor="$2"
  [[ -f "$file" ]] || return 1
  grep -qxF "$anchor" <<< "$(heading_slugs "$file")"
}

# --- release-artifact cross-reference ---------------------------------------

# Require the documented archive name to match both release workflow references.
check_release_artifact() {
  local ryml=".github/workflows/release.yml"
  local fq="docs/flash-quickstart.md"
  local zip_arg files_arg doc_arg

  # Allow empty extraction results so the checks below can report missing references.
  zip_arg="$(sed -nE 's/^[[:space:]]*run: zip -j ([A-Za-z0-9._-]+).*/\1/p' "$ryml" 2>/dev/null | head -n1)" || true
  files_arg="$(sed -nE 's/^[[:space:]]*files:[[:space:]]+([A-Za-z0-9._-]+).*/\1/p' "$ryml" 2>/dev/null | head -n1)" || true
  doc_arg="$(grep -oE '`[A-Za-z0-9._-]+\.zip`' "$fq" 2>/dev/null | sed -E 's/`//g' | head -n1)" || true

  if [[ -z "$zip_arg" || -z "$files_arg" || -z "$doc_arg" ]]; then
    add_error "release-artifact name could not be extracted from one or more sources: \
release.yml 'zip -j'='$zip_arg', release.yml 'files:'='$files_arg', docs/flash-quickstart.md='$doc_arg' \
(this usually means release.yml was restructured — update the extraction or the doc)"
    return 0
  fi
  if [[ "$zip_arg" != "$files_arg" || "$zip_arg" != "$doc_arg" ]]; then
    add_error "release-artifact name mismatch: release.yml 'zip -j'='$zip_arg', \
release.yml 'files:'='$files_arg', docs/flash-quickstart.md='$doc_arg' — the flash quickstart must name \
the archive the release workflow actually produces"
    return 0
  fi
  printf 'release-artifact name consistent across release.yml and docs/flash-quickstart.md: %s\n' "$zip_arg"
}

# --- preset-table completeness ----------------------------------------------

# Extract preset names. Assumes every "name" key belongs to configurePresets.
preset_names_from_json() {
  grep -oE '"name"[[:space:]]*:[[:space:]]*"[^"]+"' "$1" 2>/dev/null \
    | sed -E 's/^"name"[[:space:]]*:[[:space:]]*"//; s/"$//' || true
}


# Extract both role columns from the combined hardware preset table.
preset_names_from_table() {
  awk '
    /^\| Module or board \|/ { intable=1; next }
    intable && /^\|/ {
      line=$0
      while (match(line, /`[^`]+`/)) {
        print substr(line, RSTART+1, RLENGTH-2)
        line=substr(line, RSTART+RLENGTH)
      }
    }
    intable && !/^\|/ { intable=0 }
  ' "$1" || true
}


# Require all public presets in the hardware table and reject unknown or prototype presets.
check_preset_table() {
  local hw="docs/hardware.md"
  local init_json="initiator/CMakePresets.json"
  local refl_json="reflector/CMakePresets.json"
  local before=$errors
  local init_names refl_names table_presets json_presets p

  # Check each input before comparing preset sets.
  if [[ ! -f "$init_json" ]]; then
    add_error "preset-table check: source '$init_json' not found"
  fi
  if [[ ! -f "$refl_json" ]]; then
    add_error "preset-table check: source '$refl_json' not found"
  fi
  if [[ ! -f "$hw" ]]; then
    add_error "preset-table check: source '$hw' not found"
  fi
  if (( errors > before )); then
    return 0
  fi

  init_names="$(preset_names_from_json "$init_json")"
  refl_names="$(preset_names_from_json "$refl_json")"
  table_presets="$(preset_names_from_table "$hw")"

  if [[ -z "$init_names" ]]; then
    add_error "preset-table check: no configurePresets parsed from '$init_json' \
(is the file valid JSON with a non-empty configurePresets array?)"
  fi
  if [[ -z "$refl_names" ]]; then
    add_error "preset-table check: no configurePresets parsed from '$refl_json' \
(is the file valid JSON with a non-empty configurePresets array?)"
  fi
  if [[ -z "$table_presets" ]]; then
    add_error "preset-table check: no preset rows parsed from the '### Presets' \
table in '$hw' (expected a header row beginning '| Module or board |')"
  fi
  if (( errors > before )); then
    return 0
  fi

  json_presets="$(printf '%s\n%s' "$init_names" "$refl_names")"

  # Every public preset must appear in the table.
  while IFS= read -r p; do
    [[ "$p" == nrf54l15dm_* ]] && continue
    if ! grep -qxF "$p" <<< "$table_presets"; then
      add_error "preset '$p' is defined in a CMakePresets.json but missing from \
the docs/hardware.md '### Presets' table (add the row or drop the preset)"
    fi
  done <<< "$json_presets"

  # Table entries must exist and must not be prototype presets.
  while IFS= read -r p; do
    if [[ "$p" == nrf54l15dm_* ]]; then
      add_error "preset '$p' is listed in the docs/hardware.md '### Presets' \
table but is an undocumented prototype preset — remove the row"
    elif ! grep -qxF "$p" <<< "$json_presets"; then
      add_error "preset '$p' is listed in the docs/hardware.md '### Presets' \
table but no longer exists in any CMakePresets.json (drop the row or restore \
the preset)"
    fi
  done <<< "$table_presets"

  if (( errors == before )); then
    printf 'preset-table consistent: docs/hardware.md Presets table matches configurePresets in both CMakePresets.json (DM presets excluded)\n'
  fi
}

# --- preset-list cross-reference ---------------------------------------------

# Extract comma-separated presets from single-line build-wrapper invocations for a role.
preset_names_from_workflow() {
  local wf="$1" role="$2"
  # Use a delimiter that does not conflict with the script path.
  sed -nE 's|^[[:space:]]*bash ci/build\.sh --target '"$role"' --preset ([^[:space:]]+).*|\1|p' \
    "$wf" 2>/dev/null | tr ',' '\n' | sort -u || true
}


# Extract example presets for a role, including commands split across continuation lines.
preset_names_from_doc() {
  local md="$1" role="$2"
  awk -v wanted="$role" '
    {
      if (match($0, /--target[[:space:]]+(initiator|reflector)/)) {
        role = substr($0, RSTART, RLENGTH)
        sub(/--target[[:space:]]+/, "", role)
      }
      if (role == wanted && match($0, /--preset[[:space:]]+[^[:space:]]+/)) {
        tok = substr($0, RSTART, RLENGTH)
        sub(/--preset[[:space:]]+/, "", tok)
        sub(/\\+$/, "", tok)
        print tok
      }
    }
  ' "$md" 2>/dev/null | tr ',' '\n' | sort -u || true
}


# Identify prototypes excluded from release archives.
preset_unshipped() {
  local role="$1" p="$2"
  case "$p" in
    nrf54l15dm_*) return 0 ;;
  esac
  return 1
}


# Require each listed preset to exist for its role and be eligible for release.
preset_list_unknown_errors() {
  local role="$1" json="$2" json_names="$3" site="$4" names="$5" p
  while IFS= read -r p; do
    [[ -z "$p" ]] && continue
    if ! grep -qxF "$p" <<< "$json_names"; then
      add_error "preset '$p' is listed in the $role preset list in $site but \
does not exist in $json (misspelled, dropped from the build, or a name from \
the other role)"
    elif preset_unshipped "$role" "$p"; then
      add_error "preset '$p' is listed in the $role preset list in $site but \
is deliberately not shipped (see preset_unshipped in this script) — remove it \
from the list or start shipping it"
    fi
  done <<< "$names"
}


# Require every shipped preset in the release list.
preset_list_missing_errors() {
  local role="$1" json="$2" site="$3" site_names="$4" shipped="$5" p
  while IFS= read -r p; do
    [[ -z "$p" ]] && continue
    if ! grep -qxF "$p" <<< "$site_names"; then
      add_error "preset '$p' is defined in $json but missing from the $role \
preset list in $site (add it to that list, or — if it is deliberately not \
built/released — teach preset_unshipped in this script)"
    fi
  done <<< "$shipped"
}


# Require complete release lists; CI and build-guide examples may use subsets.
check_preset_lists() {
  local ci_wf=".github/workflows/ci.yml"
  local rel_wf=".github/workflows/release.yml"
  local bfs_md="docs/build-from-source.md"
  local before=$errors
  local role json json_names shipped rel_names bfs_names ci_names p

  for role in initiator reflector; do
    case "$role" in
      initiator) json="initiator/CMakePresets.json" ;;
      reflector) json="reflector/CMakePresets.json" ;;
    esac

    json_names="$(preset_names_from_json "$json")"
    rel_names="$(preset_names_from_workflow "$rel_wf" "$role")"
    bfs_names="$(preset_names_from_doc "$bfs_md" "$role")"
    ci_names="$(preset_names_from_workflow "$ci_wf" "$role")"

    # Report missing inputs or extraction failures before comparing lists.
    if [[ -z "$json_names" ]]; then
      add_error "preset-list check: no configurePresets parsed from '$json' \
(is the file valid JSON with a non-empty configurePresets array?)"
    fi
    if [[ -z "$rel_names" ]]; then
      add_error "preset-list check: no '$role' presets parsed from '$rel_wf' \
(expected a 'bash ci/build.sh --target $role --preset ...' line)"
    fi
    if [[ -z "$bfs_names" ]]; then
      add_error "preset-list check: no '$role' presets parsed from the \
build examples in '$bfs_md' (expected a command with --target $role and --preset)"
    fi
    if [[ -z "$ci_names" ]]; then
      add_error "preset-list check: no '$role' presets parsed from '$ci_wf' \
(expected a 'bash ci/build.sh --target $role --preset ...' step)"
    fi
    if (( errors > before )); then
      return 0
    fi

    # Build the expected release set, excluding deliberately unshipped presets.
    shipped=""
    while IFS= read -r p; do
      [[ -z "$p" ]] && continue
      if ! preset_unshipped "$role" "$p"; then
        shipped+="$p"$'\n'
      fi
    done <<< "$json_names"

    preset_list_unknown_errors "$role" "$json" "$json_names" "$ci_wf" "$ci_names"
    preset_list_unknown_errors "$role" "$json" "$json_names" "$rel_wf" "$rel_names"
    preset_list_unknown_errors "$role" "$json" "$json_names" "$bfs_md" "$bfs_names"

    preset_list_missing_errors "$role" "$json" "$rel_wf" "$rel_names" "$shipped"
  done

  if (( errors == before )); then
    printf 'preset lists consistent: release.yml enumerates every shipped preset; build-guide examples and CI presets exist\n'
  fi
}

# --- main -------------------------------------------------------------------

# Check tracked guides only; omit the generated changelog.
MD_FILES=()
while IFS= read -r f; do MD_FILES+=("$f"); done < \
  <(git ls-files | grep -E '^(README\.md|docs/.+\.md)$' || true)

link_re='^([0-9]+):\[(.*)\]\((.*)\)$'
ext_re='^(https?://|mailto:)'

for file in "${MD_FILES[@]}"; do
  while IFS= read -r raw; do
    [[ "$raw" =~ $link_re ]] || continue
    lineno="${BASH_REMATCH[1]}"
    text="${BASH_REMATCH[2]}"
    href="${BASH_REMATCH[3]}"
    # Remove leading whitespace and an optional title from the link destination.
    while [[ "$href" == [[:space:]]* ]]; do href="${href#?}"; done
    href="${href%%[[:space:]]*}"
    # External links are outside this offline check.
    [[ "$href" =~ $ext_re ]] && continue

    resolve_href "$file" "$href"
    target="$RESOLVED_TARGET"; anchor="$RESOLVED_ANCHOR"

    if [[ ! -f "$target" && ! -d "$target" ]]; then
      add_error "$file:$lineno: link target not found: text='$text' href='$href' -> '$target'"
      continue
    fi
    if [[ -n "$anchor" && "$target" != *.md ]]; then
      add_error "$file:$lineno: anchor '#$anchor' on non-markdown target '$target' (anchors only valid for .md)"
      continue
    fi
    if [[ -n "$anchor" && "$target" == *.md ]]; then
      if ! anchor_exists "$target" "$anchor"; then
        add_error "$file:$lineno: anchor '#$anchor' not found in '$target' (text='$text')"
        # List available anchors to help locate the broken reference.
        printf '    available heading slugs in %s:\n' "$target" >&2
        heading_slugs "$target" | sed 's/^/      - /' >&2
      fi
    fi
  done < <(emit_links "$file")
done

check_release_artifact

check_preset_table

check_preset_lists

# --- report -----------------------------------------------------------------

if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
  {
    printf '### Docs link & reference check\n\n'
    if (( errors > 0 )); then
      printf '**Failed** — %d problem(s):\n\n' "$errors"
      printf '```\n'
      printf '  - %s\n' "${summary_lines[@]}"
      printf '```\n'
    else
      printf '**Passed** — all internal links, the release-artifact name, the preset table, and the preset list sites are consistent.\n'
    fi
  } >> "$GITHUB_STEP_SUMMARY" 2>/dev/null || true
fi

if (( errors > 0 )); then
  printf 'docs check failed: %d broken link(s)/reference(s) found (see above)\n' "$errors" >&2
  exit 1
fi
printf 'docs check passed: all internal links, the release-artifact name, the preset table, and the preset list sites are consistent\n'
