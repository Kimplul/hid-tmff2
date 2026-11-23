# DOX Framework

- DOX is highly performant AGENTS.md hierarchy installed here
- Agent must follow DOX instructions across any edits

This file is the work contract for AI coding agents in this repository. The
rules for all contributors are in
[docs/CONTRIBUTING.md](./docs/CONTRIBUTING.md), section "Submitting changes".
This file adds what is specific to agents.

## Core Contract

- AGENTS.md files are binding work contracts for their subtrees
- Work products, source materials, instructions, records, assets, and durable docs must stay understandable from the nearest applicable AGENTS.md plus every parent AGENTS.md above it

## Read Before Editing

1. Read the root AGENTS.md
2. Identify every file or folder you expect to touch
3. Walk from the repository root to each target path
4. Read every AGENTS.md found along each route
5. If a parent AGENTS.md lists a child AGENTS.md whose scope contains the path, read that child and continue from there
6. Use the nearest AGENTS.md as the local contract and parent docs for repo-wide rules
7. If docs conflict, the closer doc controls local work details, but no child doc may weaken DOX

Do not rely on memory. Re-read the applicable DOX chain in the current session before editing.

## Update After Editing

Every meaningful change requires a DOX pass before the task is done.

Update the closest owning AGENTS.md when a change affects:

- purpose, scope, ownership, or responsibilities
- durable structure, contracts, workflows, or operating rules
- required inputs, outputs, permissions, constraints, side effects, or artifacts
- user preferences about behavior, communication, process, organization, or quality
- AGENTS.md creation, deletion, move, rename, or index contents

Update parent docs when parent-level structure, ownership, workflow, or child index changes. Update child docs when parent changes alter local rules. Remove stale or contradictory text immediately. Small edits that do not change behavior or contracts may leave docs unchanged, but the DOX pass still must happen.

## Hierarchy

- Root AGENTS.md is the DOX rail: project-wide instructions, global preferences, durable workflow rules, and the top-level Child DOX Index
- Child AGENTS.md files own domain-specific instructions and their own Child DOX Index
- Each parent explains what its direct children cover and what stays owned by the parent
- The closer a doc is to the work, the more specific and practical it must be

## Child Doc Shape

- Create a child AGENTS.md when a folder becomes a durable boundary with its own purpose, rules, responsibilities, workflow, materials, or quality standards
- Work Guidance must reflect the current standards of the project or user instructions; if there are no specific standards or instructions yet, leave it empty
- Verification must reflect an existing check; if no verification framework exists yet, leave it empty and update it when one exists

Default section order:
- Purpose
- Ownership
- Local Contracts
- Work Guidance
- Verification
- Child DOX Index

## Style

- Keep docs concise, current, and operational
- Document stable contracts, not diary entries
- Put broad rules in parent docs and concrete details in child docs
- Prefer direct bullets with explicit names
- Do not duplicate rules across many files unless each scope needs a local version
- Delete stale notes instead of explaining history
- Trim obvious statements, repeated rules, misplaced detail, and warnings for risks that no longer exist

## Before You Change Anything

- Read `docs/CONTRIBUTING.md` completely, and `docs/AGENTS.md` before touching
  documentation or hardware evidence.
- For force feedback work, read `docs/FFBEFFECTS.md` and the T300RS driver in
  `src/tmt300rs/` to see how effects are encoded and how the core driver
  `src/hid-tmff2.c` is used.
- Check open pull requests, issues and the human's queue branch for work on the
  same wheel or code.
- Larger restructuring (shared backends, wheel detection, renames across
  drivers) needs an issue with the maintainer first. Don't start it on your own.

## Queue Workflow

- Keep all work on the human's local queue branch. Each commit covers a single
  concern and builds on its own.
- Propose the next ready single-concern series to the human, with a short
  reason why it is ready and what it depends on. Prepare it on a new branch
  from the current `master`.
- Address review feedback with `fixup!` commits folded into the commits they
  fix. Keep the same pull request branch. Never squash a series into one
  commit, never open a replacement pull request.
- After a merge, rebase the queue onto `master` and re-cut the remaining
  series.

## Evidence

- Don't invent protocol details. Every packet field, constant and device ID
  must come from captures, descriptor dumps or tests the human has done.
- Record the evidence in `docs/` as described in `docs/AGENTS.md`, separate
  measured facts from assumptions, and reference it in the commit message.
- Don't add workarounds for behavior you can't explain.

## Commits

- Write kernel-style commit messages: driver prefix, why before what, `Link:`
  or `Fixes:` trailers with full URLs to issues or commits you have checked.
- The human adds their own `Signed-off-by` after reviewing and testing. Don't
  add `Co-authored-by` or similar trailers for AI tools.

## Working With the Human

- The human submits the change and must be able to explain it. Explain your
  changes so they can review them and answer review questions themselves.
- Don't open pull requests, post comments, resolve review threads or answer
  reviews on GitHub. Prepare the branch and text, the human submits it.
- Fill in the pull request template as instructed there, and leave the
  declaration checkboxes to the human.

## Root-Owned Areas

- `Makefile`, `Kbuild`: build of the single module `hid-tmff-new.ko` from all
  objects under `src/`, plus the `hid-tminit` submodule. A new source file must
  be added to `Kbuild`.
- `dkms/`: DKMS packaging. Keep the version in `dkms/dkms.conf` and
  `dkms/dkms-install.sh` in sync, and the module list in `dkms.conf` aligned
  with what the build produces.
- `udev/`: only `hidraw` access rules for the Steam Deck. Permissions for sysfs
  files belong to the udev rules of Oversteer, not here.
- `deps/hid-tminit`: git submodule of the separate `Kimplul/hid-tminit`
  repository, which switches wheels out of their init mode. Don't edit it here,
  changes go to that repository; only the submodule reference is updated here.
- `README.md`: user-facing installation and the list of supported wheels. Adding
  a wheel touches `src/hid-tmff2.h`, `src/hid-tmff2.c`, `Kbuild`, the udev rules
  and this list.

## Verification

- Build the module for each commit (`make`).
- Run `checkpatch.pl --no-tree` on the series.
- Check new or changed files for non-ASCII characters with
  `grep -P -n "[\x80-\xFF]" <file>`.
- Report what could not be tested, in particular on hardware.

## Closeout

1. Re-check changed paths against the DOX chain
2. Update nearest owning docs and any affected parents or children
3. Refresh every affected Child DOX Index
4. Remove stale or contradictory text
5. Run existing verification when relevant
6. Report any docs intentionally left unchanged and why

## User Preferences

When the user requests a durable behavior change, record it here or in the relevant child AGENTS.md

## Child DOX Index

- `docs/` - contributor and user documentation, protocol documentation and the
  hardware evidence per wheel.
- `src/` - kernel module sources: core driver and the wheel backends.
- `.github/` - pull request template.

The root keeps ownership of the build files, DKMS packaging, udev rules, the
README, the license and the `hid-tminit` submodule reference, see "Root-Owned
Areas".
