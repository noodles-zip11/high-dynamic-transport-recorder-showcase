# Repository Guidelines

## Project Structure & Module Organization

The current project package is under `高动态运输事件记录器_项目启动包/高动态运输事件记录器_项目启动包/`; design decisions and phased plans live in `docs/superpowers/`. As implementation begins, keep MCU code in `firmware/`, desktop tools in `host/`, ML code and datasets in `ai/`, hardware evidence in `hardware/`, automation in `scripts/`, and verification artifacts in `evidence/`. Do not commit `.venv/`, build outputs, generated IDE state, secrets, or raw private datasets.

## Build, Test, and Development Commands

STM32/RT-Thread firmware uses SCons as the canonical build system; CLion is the editor, indexer, and debugger, not a second build definition.

```powershell
pwsh scripts/selfcheck.ps1 -Mode HostOnly   # verify local tools
scons -C firmware -j4                       # build firmware
pwsh scripts/run_tests.ps1                  # run native and Python tests
python -m pytest host/tests ai/tests -q     # run Python suites directly
```

Some commands are planned but not yet implemented. Never report them as passing until the corresponding script exists and exits successfully.

## Coding Style & Naming Conventions

Use C11 and Python 3.12. Indent with four spaces; do not use tabs. C functions and files use `snake_case`, public types end in `_t`, and macros use `UPPER_SNAKE_CASE`. Keep ISRs minimal and avoid dynamic allocation in high-rate paths. Python follows PEP 8 with type hints on public APIs. Follow nearby patterns before introducing abstractions or dependencies.

## Testing Guidelines

Put native firmware tests in `firmware/tests/native/`, host tests in `host/tests/`, and ML tests in `ai/tests/`. Name Python tests `test_<behavior>.py`; name C tests `test_<module>.c`. Run the narrowest relevant test first, then the full local suite. Clearly label simulated, bench, and hardware-in-loop evidence; simulation never substitutes for hardware verification.

## Version Control, Worktrees & Pull Requests

Repository bootstrap is maintainer-only. If Git metadata is missing or invalid, stop and request approval; do not run `git init` or create the first commit automatically. After initialization, treat `main` as protected and never develop directly on it. `main` is the current software integration baseline: it must build, pass the applicable automated checks, and carry an explicit hardware-verification status. It does not imply that every board-level check has already passed.

Use one short-lived branch and one sibling worktree per active, reviewable task. Start every new task from the latest `main`:

```powershell
git worktree add ..\高动态运输事件记录器-worktrees\<slug> -b feature/<slug> main
```

Use `feature/`, `fix/`, or `docs/` branch prefixes and Conventional Commits such as `feat(storage): add event log recovery`. Keep commits atomic. A task branch ends after its software acceptance gate (review, relevant tests, build, and `git diff --check`) passes and it is integrated into `main`; do not build a long-lived chain of unmerged feature branches unless a reviewed dependency temporarily requires it.

Hardware validation is a separate quality gate. When hardware is unavailable, record the unverified items and their risks in the PR/MR and `evidence/`; never claim board acceptance from simulation or native tests. When boards are available, create a dedicated `feature/hardware-bringup` branch from the latest `main`, or a focused `fix/<subsystem>-board-validation` branch for a discovered defect. Preserve logs, screenshots, waveforms, and long-run statistics with the change that resolves the hardware finding.

Never force-push, hard-reset, delete branches/worktrees, commit, push, merge, or open a PR without explicit authorization. PRs must describe scope, tests run, hardware impact and verification status, evidence paths, remaining risks, and rollback considerations. After a merged task is confirmed safe, remove or reuse its worktree only with explicit authorization. The canonical workflow is documented in `docs/decisions/version-control-workflow.md`.

## Embedded Safety

Do not change protocol frames, pin mappings, clocks, DMA/Cache policy, Flash addresses, vector tables, OTA states, task priorities, or ISR timing without an approved design update and targeted verification.
