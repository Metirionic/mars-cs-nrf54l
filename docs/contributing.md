# Contributing

For toolchain setup and build commands, see [build from source](build-from-source.md).
Read the [architecture overview](architecture.md) before changing component
boundaries or measurement flow.

## Set up local checks

In your Python environment, install the pre-commit hooks once per clone:

```bash
pip install pre-commit
pre-commit install
```

The hooks check formatting, spelling, JSON/YAML, whitespace, line endings, and
large files. They run when you commit and may modify files; review and stage
those changes before committing again.

Run the checks before submitting a pull request:

```bash
pre-commit run --all-files
bash scripts/check-docs.sh
bash scripts/check-ncs-version.sh
```

For firmware changes, also build the affected roles and RAS/IPT presets using
the [build guide](build-from-source.md). Describe any hardware testing and
remaining validation gaps in the pull request.

Formatting and spelling rules live in
[.pre-commit-config.yaml](../.pre-commit-config.yaml),
[.clang-format](../.clang-format), and [typos.toml](../typos.toml).
Add legitimate project terms to the spelling allowlist rather than disabling
the check. CMake files use the formatter's default settings.

## Commit messages

Commits follow [Conventional Commits](https://www.conventionalcommits.org/)
and are processed by cocogitto using [cog.toml](../cog.toml):

```text
type(scope): summary
```

Use a lowercase type, an imperative summary, and no trailing period. Common
types are `feat`, `fix`, `docs`, `refactor`, `test`, `build`, `ci`, `chore`, and
`style`. Add an optional scope only when it provides useful context.
Commit-message format is checked during review rather than by the local hooks.

CI runs `cog bump --auto` on pushes to `main` to determine the release version
and generate `CHANGELOG.md`. Included commits are grouped by type, with their
summaries used as changelog entries. Use
[Keep a Changelog](https://keepachangelog.com/) as the model for readable entries,
though cocogitto's headings differ.

Write the final subject for someone reading about a release:

- Describe the resulting capability or behavior briefly in plain language.
  The subject must make sense without a ticket or review discussion.
- Keep ticket references, reviewer requests, and review-round history out of
  the subject. Put relevant traceability and rationale in the body or PR.
- Choose the type for the complete change. Corrections made while reviewing
  a new feature belong in that feature's commit.

For example:

```text
feat: support ranging on the Fanstel BM15C
```

Use the body to explain why the change was needed and describe compatibility
considerations or implementation rationale. Keep the release summary in the
subject.

Releases are managed by the [release workflow](../.github/workflows/release.yml).
Do not edit `CHANGELOG.md` or bump the release version by hand.

## Submit a pull request

Each pull request should deliver one cohesive change, including its tests and
documentation, and land as **one commit**. Development commits are fine; squash
them and fold in review fixes before merging, or use GitHub's squash merge.
Review the final commit subject and body: they should describe the complete
change, and the subject should read well as a changelog entry.

Open pull requests against `main`. Explain the problem, resulting behavior,
and how you verified the change. Update the relevant guides when user-visible
behavior, build steps, presets, or wiring requirements change.

All CI checks must pass before merging. They cover formatting, SDK-version
consistency, initiator and reflector builds, installation from a clean system,
and documentation links and preset references. The exact build coverage lives
in the [CI workflow](../.github/workflows/ci.yml); documentation checks live in
[the Docs workflow](../.github/workflows/docs.yml).

## Writing documentation

Write for a human who needs to understand the system or complete a task.
Keep documentation short, precise, and self-contained: readers should not need
access to an author's conversation, ticket history, or development session.
These rules apply equally to human-written and AI-assisted contributions.

- **Lead with what the reader needs.** State the purpose, prerequisites, and
  actionable steps. Keep paragraphs short; use tables or lists when they make
  information easier to find. Remove repetition and walls of text.
- **Describe the current system.** Leave debugging history, abandoned approaches,
  and discussions from AI conversations out of the guides. Preserve a design
  rationale only when it helps readers understand a current constraint.
- **Explain facts where they are needed.** Do not substitute obscure ticket
  numbers or private references for an explanation. Optional references must
  have descriptive links; the essential information must remain in the document.
- **Keep each document focused.** Architecture explains responsibilities and
  boundaries; guides explain tasks; hardware references explain connections and
  board requirements. Keep implementation details beside the code and link to
  existing guides instead of duplicating them.
- **Verify every claim.** Check commands, names, settings, and links against the
  source or authoritative documentation. Do not turn assumptions into facts or
  promise documentation that does not exist.

Review AI-generated text as carefully as code. Remove unnecessary detail and
conversation-specific context before committing it. Read the result as someone
new to the project, then run `bash scripts/check-docs.sh`; automated checks do
not establish that prose is clear or accurate.

## Update the Nordic SDK

The NCS version is pinned by the `nrf` revision in [west.yml](../west.yml).
Maintainers should use [scripts/bump-ncs.sh](../scripts/bump-ncs.sh) to update
it together with matching version references and the GitHub repository variable
`NCS_VERSION`.

```bash
bash scripts/bump-ncs.sh --dry-run <version>
```

Despite its name, `--dry-run` **rewrites local files** and runs validation;
it skips updating the GitHub variable and creating a pull request. Run it in
a clean working tree and review the diff. Without `--dry-run`, the script also
updates the repository variable and opens a PR; this requires GitHub access
and permission to change repository variables.

Validate version consistency locally with `bash scripts/check-ncs-version.sh`.
CI also checks the GitHub variable on pushes to `main` and during releases.
