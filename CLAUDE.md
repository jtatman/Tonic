# Project Instructions for AI Agents

This file provides instructions and context for AI coding agents working on this project.

<!-- BEGIN BEADS INTEGRATION v:1 profile:minimal hash:1105d646 -->
## Beads Issue Tracker

This project uses **bd (beads)** for issue tracking. Run `bd prime` to see full workflow context and commands.

### Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --claim  # Claim work
bd close <id>         # Complete work
```

### Rules

- Use `bd` for ALL task tracking — do NOT use TodoWrite, TaskCreate, or markdown TODO lists
- Run `bd prime` for detailed command reference and session close protocol
- Use `bd remember` for persistent knowledge — do NOT use MEMORY.md files

**Architecture in one line:** issues live in a local Dolt DB; sync uses `refs/dolt/data` on your git remote; `.beads/issues.jsonl` is a passive export. See https://github.com/gastownhall/beads/blob/main/docs/core-concepts/sync-concepts.md for details and anti-patterns.

## Agent Context Profiles

The managed Beads block is task-tracking guidance, not permission to override repository, user, or orchestrator instructions.

- **Conservative (default)**: Use `bd` for task tracking. Do not run git commits, git pushes, or Dolt remote sync unless explicitly asked. At handoff, report changed files, validation, and suggested next commands.
- **Minimal**: Keep tool instruction files as pointers to `bd prime`; use the same conservative git policy unless active instructions say otherwise.
- **Team-maintainer**: Only when the repository explicitly opts in, agents may close beads, run quality gates, commit, and push as part of session close. A current "do not commit" or "do not push" instruction still wins.

## Session Completion

This protocol applies when ending a Beads implementation workflow. It is subordinate to explicit user, repository, and orchestrator instructions.

1. **File issues for remaining work** - Create beads for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **Handle git/sync by active profile**:
   ```bash
   # Conservative/minimal/default: report status and proposed commands; wait for approval.
   git status

   # Team-maintainer opt-in only, unless current instructions forbid it:
   git pull --rebase
   git push
   git status
   ```
5. **Hand off** - Summarize changes, validation, issue status, and any blocked sync/commit/push step

**Critical rules:**
- Explicit user or orchestrator instructions override this Beads block.
- Do not commit or push without clear authority from the active profile or the current user request.
- If a required sync or push is blocked, stop and report the exact command and error.
<!-- END BEADS INTEGRATION -->



<!-- BEGIN PROJECT SECTION (keep identical in CLAUDE.md and AGENTS.md) -->
## Workflow and tooling

- **Tasks / short-term memory:** `bd` (beads). Start with `bd ready`; Phase 0 epic is `Tonic-ob6`.
- **Long-term memory and development process:** `mnemoria` (store in `./mnemoria`, committed to git).
  Read: `mnemoria search "<topic>"` / `mnemoria ask "<question>"`.
  Write: `mnemoria add -a <agent> -t decision|discovery|intent --summary "<one line>" "<details>"`.
  Process rules live in mnemoria, not in this file.
- **Code analysis:** `codegraph` (index in `.codegraph/`, not committed). Refresh with `codegraph sync`.
- **Git:** commit after every significant change and at checkpoints. Work on branches
  (Phase 0 and the Phase 1 design are merged to `master`); `origin` is the jtatman/Tonic fork. The new core lives in jtatman/tonic2.
- Keep this section identical in `CLAUDE.md` and `AGENTS.md`.

## Build & Test

Builds run in WSL (Ubuntu, g++ 15, cmake 4.x) or with Visual Studio 2022 on Windows (its bundled cmake is not on PATH: `C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin`). CI covers Linux, Windows (MSVC) and macOS.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # options: TONIC_BUILD_TESTS (ON), TONIC_BUILD_EXAMPLES (OFF)
cmake --build build -j
ctest --test-dir build --output-on-failure        # Tests/regression_tests.cpp, offline renders, no audio device
```

From Windows: `wsl.exe -e bash -lc 'cmake -S /mnt/c/Users/stora/Downloads/vsts/Tonic -B /tmp/tonic-cmake && cmake --build /tmp/tonic-cmake -j8 && ctest --test-dir /tmp/tonic-cmake'`

## Architecture Overview

- `src/Tonic/` ~12k LOC, C++17, Unlicense (public domain). `src/Tonic.h` is the umbrella header.
- Pull-based graph processed in fixed 64-sample blocks (`kSynthesisBlockSize`). `Generator` = audio rate,
  `ControlGenerator` = once per block. Public classes are ref-counted handles (`TonicSmartPointer`) around
  `Tonic_::Xxx_` implementation classes.
- `BufferFiller_` -> `Synth` / `Mixer` turn the graph into interleaved float buffers (`fillBufferOfFloats`).
- `TonicCore.h`: global sample rate (`setSampleRate`, C++17 inline variable), helpers, mutex macros (std::mutex).
- Known structural limits (Phase 1 redesign): non-atomic refcounts, lock on audio thread, mono/stereo only,
  no polyphony, no sample-accurate events, one global sample rate.

## Conventions & Patterns

- New units follow the handle/impl pair + `TONIC_MAKE_GEN_SETTERS` / `TONIC_MAKE_CTRL_GEN_SETTERS` macros.
- Every bug fix gets a regression check in `Tests/regression_tests.cpp`.
- Plugin formats (VST3/CLAP/LV2/AU/JACK) belong in a wrapper (DPF), never in the core library.
<!-- END PROJECT SECTION -->
