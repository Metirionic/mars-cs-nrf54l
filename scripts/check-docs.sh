#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Docs link & reference checker for mars-cs-nrf54l.
#
# Catches the most likely documentation/reference drift before a user hits it:
#   1. Internal Markdown links between README.md and the docs/ guides are
#      validated — every [text](href) must resolve to a tracked file (and to a
#      real heading when an #anchor is present), so a renamed or moved guide is
#      caught.
#   2. The release-artifact name referenced by docs/flash-quickstart.md must
#      match the archive .github/workflows/release.yml produces/publishes.
#   3. The preset names in docs/hardware.md's "### Presets" table must match the
#      configurePresets in initiator/CMakePresets.json and
#      reflector/CMakePresets.json — every preset must be listed, and every
#      listed preset must still exist — so a preset added to the build but left
#      out of the docs (or dropped from the build but left in the docs) is
#      caught. The two nRF54L15 DM presets are excluded (deliberately
#      undocumented; see issue #89).
#   4. The preset lists hand-copied at the six list sites — the ci.yml build
#      matrix, the release.yml build-all commands, and the two mirrored
#      commands in docs/build-from-source.md — must enumerate the shipped
#      configurePresets of their role's CMakePresets.json (see
#      check_preset_lists), so a misspelled or dropped preset name in one of
#      those lists is caught at docs-check time instead of by a release run or
#      a user reproducing the documented command.
#
# External http(s)/mailto: URLs are deliberately NOT checked (out of scope; they
# would add network flakiness). Only internal links, and the release-artifact,
# preset-table, and preset-list cross-references are validated.
#
# Only inline [text](href) links are parsed — reference-style links
# ([text][ref] + [ref]: url) and autolinks (<url>) are not. The repo's docs use
# inline links exclusively, so this covers every link actually in use.
#
# Heading anchors are matched against a simplified GitHub-style slug (lowercase,
# punctuation stripped, spaces -> hyphens). GitHub's duplicate-heading -N suffix
# and inline formatting inside a heading are not modeled; no current doc has
# duplicate headings or formatting in an anchor-target heading.
#
# Runs in CI via .github/workflows/docs.yml and locally: `bash scripts/check-docs.sh`.

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

errors=0
summary_lines=()

# Probe once whether realpath supports the GNU -m --relative-to flags we use.
# BSD realpath (macOS 12.3+) exists but lacks these flags, so normalize_relpath
# is the fallback there and on systems without realpath at all.
if realpath -m --relative-to=. / >/dev/null 2>&1; then
  HAS_REALPATH=1
else
  HAS_REALPATH=0
fi

# add_error <message> — record one problem on stderr and in the CI summary list.
add_error() {
  local msg="$1"
  printf '  - %s\n' "$msg" >&2
  summary_lines+=("$msg")
  errors=$((errors + 1))
}

# --- link parsing -----------------------------------------------------------

# emit_links <file> -> "lineno:[text](href)" per inline link, one match per
# line. grep -o emits each match separately (handles lines with >1 link).
# Code-span tokens (e.g. `cs-ranging-firmware.zip`, `*.hex`) lack the
# [text](href) syntax and so are never matched.
emit_links() {
  grep -noE '\[[^]]+\]\([^)]+\)' "$1" || true
}

# --- href resolution --------------------------------------------------------

# normalize_relpath <path> -> collapse ./ and ../ segments (pure bash, portable
# fallback for environments whose realpath lacks -m / --relative-to, e.g. macOS).
normalize_relpath() {
  local p="$1" part idx
  local -a parts=() stack=()
  # local IFS scopes the '/' to this function (auto-restored on return): it
  # splits the read into parts and joins ${stack[*]} back with '/'.
  local IFS='/'
  read -r -a parts <<< "$p"
  for part in "${parts[@]}"; do
    case "$part" in
      ''|'.') ;;
      '..')
        # Pop a real component; otherwise keep/push a '..' so '..' overflow
        # isn't silently dropped. Matches GNU `realpath -m --relative-to` for
        # every in-repo path (no symlinked dirs); GNU additionally collapses at
        # the filesystem root, which no link here reaches. Compute the top index
        # once so the pop test and the unset can't drift apart.
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
  # GNU `realpath -m --relative-to` returns "." when all segments cancel; an
  # empty stack would otherwise yield "" (and a confusing "target ''" message).
  [[ -n "$out" ]] || out='.'
  printf '%s\n' "$out"
}

# resolve_href <linker_file> <href> -> sets RESOLVED_TARGET, RESOLVED_ANCHOR.
# The path part is resolved relative to the LINKING file's directory (not the
# repo root), because link text frequently lies (text="docs/foo.md" but
# href="foo.md"). Anchor-only links (#anchor) resolve to the linking file itself.
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

# slugify <heading_text> -> GitHub-style slug (lowercase, strip punctuation,
# spaces -> hyphens, trim leading/trailing hyphens).
slugify() {
  local h="$1"
  h="$(printf '%s' "$h" | tr '[:upper:]' '[:lower:]')"
  h="$(printf '%s' "$h" | tr -cd 'a-z0-9 _-')"
  h="${h// /-}"
  h="${h#-}"; h="${h%-}"
  printf '%s\n' "$h"
}

# heading_slugs <md_file> -> one slug per ATX heading. The trailing `|| true`
# keeps the function returning 0 when grep finds no headings — without it,
# `set -e`+`pipefail` would abort the pipeline before any trailing `return`,
# both when captured via $(...) and when called directly in a pipeline (e.g.
# the diagnostic slug dump in the main loop).
heading_slugs() {
  local file="$1"
  grep -nE '^#{1,6} ' "$file" 2>/dev/null \
    | sed -E 's/^[0-9]+:#*[[:space:]]+//' \
    | while IFS= read -r h; do slugify "$h"; done || true
}

# anchor_exists <md_file> <anchor> -> 0 if a heading slug equals the anchor.
anchor_exists() {
  local file="$1" anchor="$2"
  [[ -f "$file" ]] || return 1
  grep -qxF "$anchor" <<< "$(heading_slugs "$file")"
}

# --- release-artifact cross-reference ---------------------------------------

# Assert the archive name release.yml PRODUCES (zip -j) and PUBLISHES (files:)
# matches the name docs/flash-quickstart.md tells users to download. Three
# sources, so a restructuring of any single line fails loudly instead of
# silently passing.
check_release_artifact() {
  local ryml=".github/workflows/release.yml"
  local fq="docs/flash-quickstart.md"
  local zip_arg files_arg doc_arg

  # Each extraction pipeline ends in `|| true` so a no-match (or an unreadable
  # source file) under `set -e`+`pipefail` falls through to the empty-string
  # fail-loud check below instead of aborting the script with a bare exit 1.
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

# preset_names_from_json <file> -> one configurePresets name per line. The only
# "name" keys in CMakePresets.json are preset names inside configurePresets
# ("displayName" never matches the quoted "name" key — different case and no
# leading quote — and no value carries a "name" substring between quotes), so a
# flat grep suffices and avoids a jq dependency. The trailing `|| true` keeps a
# missing/unreadable file from aborting under set -e; an empty result is caught
# by check_preset_table's fail-loud guards.
preset_names_from_json() {
  grep -oE '"name"[[:space:]]*:[[:space:]]*"[^"]+"' "$1" 2>/dev/null \
    | sed -E 's/^"name"[[:space:]]*:[[:space:]]*"//; s/"$//' || true
}

# preset_names_from_table <md> -> the first-column backticked name of every row
# in the "### Presets" table. The table is scoped by its unique "| Preset | Mode
# | ..." header (no other table in hardware.md has a "Preset" first column); the
# header and separator rows are skipped, and parsing stops at the first
# non-table line. Only backticked first cells are emitted, so any row without
# one is silently ignored rather than misparsed.
preset_names_from_table() {
  awk '
    /^\| Preset \|/ { intable=1; next }
    intable && /^\|[-[:space:]]+\|/ { next }
    intable && /^\|/ {
      line=$0
      sub(/^\|[[:space:]]*/, "", line)
      if (match(line, /^`[^`]+`/)) print substr(line, 2, RLENGTH-2)
    }
    intable && !/^\|/ { intable=0 }
  ' "$1" || true
}

# Assert every configurePresets name in initiator/CMakePresets.json and
# reflector/CMakePresets.json appears in docs/hardware.md's "### Presets" table,
# and every preset the table names still exists in some CMakePresets.json. This
# is the gap that let the _ipt presets ship via the release archive while the
# docs table silently omitted them (see #87). The two nRF54L15 DM presets
# (nrf54l15dm_*) are deliberately undocumented — a non-purchasable prototype,
# per #89 — so they are excluded from the "must appear in the table" direction
# and flagged if they appear in the table at all. Only configurePresets names
# are read (buildPresets and the other name-bearing arrays are not the docs
# surface).
check_preset_table() {
  local hw="docs/hardware.md"
  local init_json="initiator/CMakePresets.json"
  local refl_json="reflector/CMakePresets.json"
  local before=$errors
  local init_names refl_names table_presets json_presets p

  # Fail loud per source (mirrors check_release_artifact's fail-loud pattern,
  # but per-file so one missing CMakePresets.json can't silently degrade the
  # check to "only the other role's presets matter").
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
table in '$hw' (expected a header row beginning '| Preset | Mode | Role |')"
  fi
  if (( errors > before )); then
    return 0
  fi

  json_presets="$(printf '%s\n%s' "$init_names" "$refl_names")"

  # Direction (a): every documented (non-DM) preset must appear in the table.
  while IFS= read -r p; do
    [[ "$p" == nrf54l15dm_* ]] && continue
    if ! grep -qxF "$p" <<< "$table_presets"; then
      add_error "preset '$p' is defined in a CMakePresets.json but missing from \
the docs/hardware.md '### Presets' table (add the row or drop the preset)"
    fi
  done <<< "$json_presets"

  # Direction (b): every table row must name a preset that still exists, and
  # must not name a deliberately-undocumented DM preset.
  while IFS= read -r p; do
    if [[ "$p" == nrf54l15dm_* ]]; then
      add_error "preset '$p' is listed in the docs/hardware.md '### Presets' \
table but is a deliberately-undocumented DM preset (see #89) — remove the row"
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

# preset_names_from_workflow <workflow> <role> -> one --preset name per line,
# de-duplicated, from every `bash ci/build.sh --target <role> --preset <list>`
# invocation in the workflow (a list is comma-separated). In ci.yml that
# unions the build-matrix step with the single-preset onboarding steps; in
# release.yml it is the build-all step. The trailing `|| true` keeps a
# missing/unreadable file from aborting under set -e (an empty result is caught
# by check_preset_lists' fail-loud guards, as in check_preset_table).
preset_names_from_workflow() {
  local wf="$1" role="$2"
  # '|' as the sed delimiter: the pattern itself contains slashes (ci/build.sh).
  sed -nE 's|^[[:space:]]*bash ci/build\.sh --target '"$role"' --preset ([^[:space:]]+).*|\1|p' \
    "$wf" 2>/dev/null | tr ',' '\n' | sort -u || true
}

# preset_names_from_doc <md> <role> -> one --preset name per line,
# de-duplicated, from the role's command in "### Multiple presets (driven like
# CI)" in docs/build-from-source.md. The section is scoped by its unique
# heading, so the single-preset pair examples earlier in the guide are not
# swept in; the role of a continuation line ('  --preset <names> \') is the
# --target seen on the command's first line.
preset_names_from_doc() {
  local md="$1" role="$2"
  awk -v wanted="$role" '
    /^[[:space:]]*#+ / { insec = ($0 ~ /Multiple presets \(driven like CI\)/); next }
    !insec { next }
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

# preset_unshipped <role> <preset> -> 0 when the preset is deliberately NOT
# built by the release workflow, i.e. must be absent from (and is not demanded
# of) the list sites:
#   * nrf54l15dm_* — deliberately undocumented prototypes (see #89, as in
#     check_preset_table), and
#   * the non-shipped nRF54L15 DK antenna variants — documented in
#     docs/hardware.md as bench configurations but deliberately not released
#     (nrf54l15dk_cent_a1_2 / nrf54l15dk_cent_a4_4 for the initiator,
#     nrf54l15dk_peri_a2_2 / nrf54l15dk_peri_a4_4 for the reflector).
# A preset added to (or dropped from) a CMakePresets.json fails
# check_preset_lists until it is either shipped by the release/doc lists or
# taught to this predicate as deliberately unshipped — so the decision is made
# in one place, loudly.
preset_unshipped() {
  local role="$1" p="$2"
  case "$p" in
    nrf54l15dm_*) return 0 ;;
    nrf54l15dk_cent_a1_2|nrf54l15dk_cent_a4_4)
      [[ "$role" == initiator ]] && return 0 ;;
    nrf54l15dk_peri_a2_2|nrf54l15dk_peri_a4_4)
      [[ "$role" == reflector ]] && return 0 ;;
  esac
  return 1
}

# preset_list_unknown_errors <role> <json> <json_names> <site> <names> —
# direction (a): every preset name listed at one site must be a shipped preset
# of that role. A misspelling, a preset dropped from the build, a name copied
# from the other role, and a deliberately-unshipped preset all land here.
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

# preset_list_missing_errors <role> <json> <site> <site_names> <shipped> —
# direction (b): completeness. Every shipped preset of the role must be
# enumerated by the site's list, so a preset added to or dropped from
# CMakePresets.json forces the published lists to move with it.
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

# The six list sites enumerate the preset matrix by hand; this asserts they
# match the CMakePresets.json files. Expectation per role (initiator vs
# reflector), derived from how the sites read today:
#   * release.yml's role list and docs/build-from-source.md's role list must
#     enumerate exactly the role's SHIPPED preset set — every configurePresets
#     name in the role's CMakePresets.json except the deliberately-unshipped
#     ones (preset_unshipped above). Enforced in both directions.
#   * ci.yml's role list is a deliberate smoke subset of the shipped set (it
#     builds 9 of the 16 shipped presets and omits, among others, the ublox,
#     Ezurio and Fanstel carriers), so only the subset direction is enforced
#     there: every preset ci.yml names must be shipped. Completeness stays
#     anchored on the release/doc lists, which fail whenever the preset set
#     changes — so the ci.yml subset has to be a conscious choice.
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

    # Fail loud per extraction (mirrors check_preset_table): a restructured
    # or unreadable source must not silently pass as "no mismatch".
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
'Multiple presets (driven like CI)' section of '$bfs_md' (expected a \
'--target $role' command with a '  --preset <names>' continuation line)"
    fi
    if [[ -z "$ci_names" ]]; then
      add_error "preset-list check: no '$role' presets parsed from '$ci_wf' \
(expected a 'bash ci/build.sh --target $role --preset ...' step)"
    fi
    if (( errors > before )); then
      return 0
    fi

    # The shipped set for this role (see preset_unshipped for the exclusions).
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
    preset_list_missing_errors "$role" "$json" "$bfs_md" "$bfs_names" "$shipped"
  done

  if (( errors == before )); then
    printf 'preset lists consistent: release.yml and docs/build-from-source.md enumerate every shipped preset for both roles; ci.yml builds a subset of the shipped set\n'
  fi
}

# --- main -------------------------------------------------------------------

# Scope per issue #16: README.md + docs/*.md, tracked files only. git ls-files
# never lists gitignored content (e.g. docs/agents/*.md), so a checkout has no
# live links into gitignored files to false-positive on. CHANGELOG.md is
# excluded (not a guide, auto-generated, no internal links).
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
    # Strip an optional link title and any leading whitespace: CommonMark allows
    # whitespace after '(' and an optional "title" after the destination, while
    # the destination itself contains no raw spaces. Trim leading whitespace
    # first (else a leading space would collapse the href to empty and the link
    # would silently resolve to the linking file itself), then cut at the first
    # whitespace to drop any title / trailing whitespace.
    while [[ "$href" == [[:space:]]* ]]; do href="${href#?}"; done
    href="${href%%[[:space:]]*}"
    # Skip external URLs — out of scope (no network checks, no flakiness).
    [[ "$href" =~ $ext_re ]] && continue

    resolve_href "$file" "$href"
    target="$RESOLVED_TARGET"; anchor="$RESOLVED_ANCHOR"

    if [[ ! -f "$target" ]]; then
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
        # Dump the target's actual heading slugs to aid diagnosis.
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
