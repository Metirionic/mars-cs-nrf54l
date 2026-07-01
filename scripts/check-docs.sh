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
#
# External http(s)/mailto: URLs are deliberately NOT checked (out of scope; they
# would add network flakiness). Only internal links and the release-artifact
# cross-reference are validated.
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
  local p="$1" part
  local -a parts=() stack=()
  # local IFS scopes the '/' to this function (auto-restored on return): it
  # splits the read into parts and joins ${stack[*]} back with '/'.
  local IFS='/'
  read -r -a parts <<< "$p"
  for part in "${parts[@]}"; do
    case "$part" in
      ''|'.') ;;
      '..')
        # Pop a real component; but if the stack is empty or the top is itself a
        # '..' (already above the base), keep the '..' so overflow matches GNU
        # `realpath -m --relative-to` instead of being silently dropped.
        if ((${#stack[@]})) && [[ "${stack[$((${#stack[@]}-1))]}" != '..' ]]; then
          unset "stack[$((${#stack[@]}-1))]"
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
      printf '**Passed** — all internal links and the release-artifact name are consistent.\n'
    fi
  } >> "$GITHUB_STEP_SUMMARY" 2>/dev/null || true
fi

if (( errors > 0 )); then
  printf 'docs check failed: %d broken link(s)/reference(s) found (see above)\n' "$errors" >&2
  exit 1
fi
printf 'docs check passed: all internal links and the release-artifact name are consistent\n'
