# PR Split Plan — pg_auto_failover #1125

Seven sequenced PRs extracted from the `pgaftest-infra` branch (48 commits, 158 files).
Each section is a self-contained brief for a new session.

## Critical rebase note

The commit history is NOT pre-split. The founding commit `a99e035` alone touches
common/, pg_autoctl commands, pgaftest, Dockerfile, CI, and specs simultaneously.
Later "fix CI" commits mix C source with spec changes in the same patch.

**You cannot split by cherry-picking commits at existing boundaries.**
Each PR requires cherry-picking individual FILE changes out of mixed commits,
then squashing and cleaning up. Expect 2–4 hours of interactive rebase work
spread across sessions.

Branch to split: `pgaftest-infra`
Base: `main`
Upstream: `hapostgres/pg_auto_failover`

---

## PR 1 — CI modernisation & style check

**Goal:** Update GitHub Actions dependencies and add an authoritative style-check job.
No source code changes. Merges first, unblocks everything else.

### Files to change

- `.github/workflows/run-tests.yml`
- `Makefile` (CI-related targets only: `citus_indent`, `banned` targets)
- `.gitattributes` (add `*.pgaf linguist-language=Ruby` or similar if present)
- `.gitignore` (add pgaftest build artefacts: `*.tab.c`, `*.tmp`)

### What to do

1. Create branch `ci-modernisation` off `main`.

2. From `2a8efff` cherry-pick ONLY the `run-tests.yml` and `Makefile` CI changes:
   ```
   git checkout pgaftest-infra -- .github/workflows/run-tests.yml
   ```
   Then **revert** any lines that reference pgaftest-specific jobs
   (those belong in PR 7's `run-pgaftest.yml`).

3. Changes to make in `run-tests.yml`:
   - `actions/checkout@v3` → `actions/checkout@v4.2.2` everywhere
   - Remove the `PG14 linting` matrix entry
   - Add a new `style_checker` job:
     ```yaml
     style_checker:
       name: Style check
       runs-on: ubuntu-latest
       container: citus/stylechecker:no-py
       steps:
         - uses: actions/checkout@v4.2.2
         - run: git config --global --add safe.directory ${GITHUB_WORKSPACE}
         - run: citus_indent --check
         - run: ci/banned.h.sh
     ```
   - Add PG17 to the PGVERSION matrix (was missing, only 13–16+18)
   - Remove `TRAVIS_BUILD_DIR` env var export (dead code from old CI system)

4. From `639913a` cherry-pick the `.gitignore` additions only.

5. From `678c43e` / `d4acb65` cherry-pick `.gitattributes` if present.

6. Verify: `git diff main HEAD --stat` should show only CI/meta files.

### PR description

"Update GitHub Actions to v4, add authoritative citus_indent style-check job
using `citus/stylechecker:no-py`, add PG17 to test matrix, remove TRAVIS_BUILD_DIR
remnants. No source changes."

---

## PR 2 — Bug fixes in pg_autoctl (backportable)

**Goal:** Ship the targeted C fixes independently. These fix real production bugs
and can be backported to v2.1. No structural changes.

### Files to change

- `src/bin/pg_autoctl/cli_common.c`
- `src/bin/pg_autoctl/cli_drop_node.c` + `file_utils.h` include
- `src/bin/pg_autoctl/cli_create_node.c`
- `src/bin/pg_autoctl/service_keeper.c` (minimal)
- `src/bin/pg_autoctl/fsm_transition.c` (if part of a173911)

### What to do

1. Create branch `fix-pg-autoctl-bugs` off `main`.

2. Cherry-pick commit `7c093af` entirely (it's clean: SIGHUP fix, report_lsn timing, drop timeout):
   ```
   git cherry-pick 7c093af
   ```

3. Cherry-pick commit `afe91c3` entirely (drop-node state file):
   ```
   git cherry-pick afe91c3
   ```

4. Cherry-pick commit `504c6b7` entirely (hba-lan pgSetup defer):
   ```
   git cherry-pick 504c6b7
   ```

5. From `a173911` cherry-pick ONLY the C source changes (NOT Dockerfile or spec changes):
   ```
   git checkout pgaftest-infra -- src/bin/pg_autoctl/fsm_transition.c
   # verify it's only the upgrade build fix, not touching anything else
   git diff HEAD
   ```

6. Run style check: `docker run --rm -v $(pwd):/mnt citus/stylechecker:no-py citus_indent --check`

### Tests to verify

These fixes are validated by the existing Python test suite (`Run Tests` workflow).
The specific failing scenarios:
- SIGHUP fix: `test_basic_operation.py` reload step
- Drop-node fix: `test_basic_operation.py` drop secondary step
- hba-lan fix: any test that exercises `pg_autoctl create postgres --auth lan`

### PR description

"Three backportable bug fixes:
1. SIGHUP PID-reuse race in `pg_autoctl reload` — `signal(SIGHUP, SIG_IGN)` at
   start of `cli_pg_autoctl_reload` prevents the broadcast SIGHUP from killing
   a freshly exec'd one-shot command (exit 129).
2. Drop-node fails when service already cleaned up state file — add `else if`
   branch in `cli_drop_local_node` that treats a missing state file as confirmation
   of successful drop (pid ≠ 0 but state file gone).
3. hba-lan: defer pgSetup init until after config file exists."

---

## PR 3 — Remove Azure integration

**Goal:** Delete ~3 500 lines of unmaintained Azure scaffolding. Pure deletion,
no logic changes, unblocks the command reorganisation in PR 4.

### Files to delete entirely

```
src/bin/pg_autoctl/azure.c
src/bin/pg_autoctl/azure.h
src/bin/pg_autoctl/azure_config.c
src/bin/pg_autoctl/azure_config.h
src/bin/pg_autoctl/cli_do_azure.c
src/bin/pg_autoctl/cli_do_tmux_azure.c
```

### Files to modify

- `src/bin/pg_autoctl/cli_do_root.c` — remove azure subcommand registrations
- `src/bin/pg_autoctl/cli_do_root.h` — remove azure extern declarations
- `src/bin/pg_autoctl/Makefile` — remove azure*.c and cli_do_azure.c from OBJS

### What to do

1. Create branch `remove-azure` off `main` (or off PR 2 if that's merged first —
   doesn't matter, no file overlap).

2. Extract the azure deletions from `a99e035`:
   ```
   git rm src/bin/pg_autoctl/azure.c src/bin/pg_autoctl/azure.h \
          src/bin/pg_autoctl/azure_config.c src/bin/pg_autoctl/azure_config.h \
          src/bin/pg_autoctl/cli_do_azure.c src/bin/pg_autoctl/cli_do_tmux_azure.c
   ```

3. Edit `cli_do_root.c` to remove the azure extern and subcommand table entry.
   Look for `azure_commands` and `cli_do_tmux_azure_commands` references.

4. Edit `cli_do_root.h` to remove azure extern declarations.

5. Edit `Makefile` to remove azure sources from `OBJS`.

6. Build check: `make -C src/bin/pg_autoctl` should compile cleanly.

### PR description

"Remove unmaintained Azure integration (~3 500 lines). The `pg_autoctl do azure`
and `pg_autoctl do tmux azure` subcommands are deleted with no replacement —
the Azure tutorial was already broken and the code had no active users."

---

## PR 4 — pg_autoctl command reorganisation (do → inspect / manual)

**Goal:** Rename `do` to `internal` (stays hidden, subprocess-only entry points).
Add always-visible `inspect` (read-only diagnostics) and `manual` (FSM override ops)
subcommand trees. Update Python tests. Docs.

**Depends on:** PR 3 merged (azure entries gone from `cli_do_root.c`).

### New files

- `src/bin/pg_autoctl/cli_inspect.c` + `.h`
- `src/bin/pg_autoctl/cli_manual.c` + `.h`

### Modified files

- `src/bin/pg_autoctl/cli_do_root.c` + `.h`
- `src/bin/pg_autoctl/cli_root.c`
- `src/bin/pg_autoctl/cli_do_fsm.c` (dispatch table name changes)
- `src/bin/pg_autoctl/cli_do_misc.c`
- `src/bin/pg_autoctl/cli_do_monitor.c`
- `src/bin/pg_autoctl/cli_do_service.c`
- `src/bin/pg_autoctl/cli_do_coordinator.c`
- `src/bin/pg_autoctl/cli_get_set_properties.c` (moved under inspect/manual)
- `docs/ref/pg_autoctl_do_pgsetup.rst`

### What to do

1. Create branch `pg-autoctl-commands` off PR 3 (or rebase onto main once PR 3 merges).

2. The three relevant commits (`a99e035` command parts, `22ab675`, `bfd0dc4`, `21560e4`)
   went through: `override` → `manual` → `internal` for the hidden tree.
   The final state is:
   - `pg_autoctl inspect …` — always visible, read-only diagnostics
   - `pg_autoctl manual …` — always visible, FSM override operations
   - `pg_autoctl internal …` — hidden, subprocess entry points for the supervisor

3. Extract the command files from `pgaftest-infra`:
   ```
   git checkout pgaftest-infra -- \
     src/bin/pg_autoctl/cli_inspect.c \
     src/bin/pg_autoctl/cli_inspect.h \
     src/bin/pg_autoctl/cli_manual.c \
     src/bin/pg_autoctl/cli_manual.h \
     src/bin/pg_autoctl/cli_do_root.c \
     src/bin/pg_autoctl/cli_do_root.h \
     src/bin/pg_autoctl/cli_root.c \
     src/bin/pg_autoctl/cli_do_fsm.c \
     src/bin/pg_autoctl/cli_do_misc.c \
     src/bin/pg_autoctl/cli_do_monitor.c \
     src/bin/pg_autoctl/cli_do_service.c \
     src/bin/pg_autoctl/cli_do_coordinator.c \
     src/bin/pg_autoctl/cli_get_set_properties.c \
     docs/ref/pg_autoctl_do_pgsetup.rst
   ```
   Then manually verify each file: remove any pgaftest-specific references
   (e.g. references to `pgaftest` in Makefile, any `cli_override` leftovers).

4. Add `cli_inspect.c` and `cli_manual.c` to `src/bin/pg_autoctl/Makefile` OBJS.

5. Run: `pg_autoctl --help` should show `inspect` and `manual` at top level.
   `pg_autoctl internal --help` should be hidden (not in top-level help).

6. Update Python tests: grep for `pg_autoctl do ` → `pg_autoctl inspect `
   or `pg_autoctl manual ` as appropriate. Commits `2a8efff` has the test changes.

### PR description

"Reorganise pg_autoctl command surface:
- `pg_autoctl inspect` (new, always visible): read-only diagnostics — wraps the
  old `do show`, `do pgsetup`, `do monitor`, `do service getpid` subcommands
- `pg_autoctl manual` (new, always visible): FSM override operations — wraps the
  old `do fsm assign/step`, `do primary`, `do standby`, `do coordinator` subcommands
- `pg_autoctl internal` (hidden): subprocess entry points for the supervisor
  (was `do`, stays hidden, same purpose)
- `PG_AUTOCTL_DEBUG` no longer gates command visibility"

---

## PR 5 — pg_autoctl node + node.ini

**Goal:** New `pg_autoctl node` subcommand: declarative node spec file (`node.ini`)
and supervisor file-watcher that re-reads it on SIGHUP.

**Depends on:** PR 4 merged (node hangs off the reorganised command tree).

### New files

- `src/bin/pg_autoctl/cli_node.c` + `.h`
- `src/bin/pg_autoctl/nodespec.c` + `.h`
- `docs/ref/pg_autoctl_node.rst`

### Modified files

- `src/bin/pg_autoctl/cli_root.c` (add node subcommand)
- `src/bin/pg_autoctl/keeper_config.c` + `.h`
- `src/bin/pg_autoctl/supervisor.c` + `.h` (SIGHUP handler)
- `src/bin/pg_autoctl/Makefile` (add cli_node.o, nodespec.o)
- `docs/index.rst`

### What to do

1. Create branch `pg-autoctl-node` off PR 4 (or rebase once PR 4 merges).

2. Extract from `pgaftest-infra`:
   ```
   git checkout pgaftest-infra -- \
     src/bin/pg_autoctl/cli_node.c \
     src/bin/pg_autoctl/cli_node.h \
     src/bin/pg_autoctl/nodespec.c \
     src/bin/pg_autoctl/nodespec.h \
     docs/ref/pg_autoctl_node.rst \
     docs/index.rst
   ```

3. From `fe341da` extract the supervisor.c SIGHUP watcher changes and keeper_config changes.
   Do NOT take the pgaftest-related parts of `c896747`.

4. Add `cli_node.o nodespec.o` to `src/bin/pg_autoctl/Makefile`.

5. Register `node_commands` in `cli_root.c` (visible, no PG_AUTOCTL_DEBUG gate).

6. Verify: `pg_autoctl node --help` shows `run`, `show`, `edit` subcommands.
   `pg_autoctl node run --pgdata /tmp/test` should read `node.ini` from PGDATA.

### Note on optional deferral

This PR can be deferred or kept merged with PR 4 if the node.ini work needs
more iteration. PR 4 is a self-contained rename and is shippable without PR 5.

### PR description

"Add `pg_autoctl node` subcommand tree: declarative node spec file (node.ini)
lets operators manage node configuration as a versioned file rather than a
sequence of `pg_autoctl set` commands. The supervisor file-watcher re-reads
node.ini on SIGHUP, applying changes without a full restart."

---

## PR 6 — src/bin/common/ shared library build

**Goal:** Move ~20 generic utility source files out of `src/bin/pg_autoctl/` into
`src/bin/common/` (a new static library). Both `pg_autoctl` and `pgaftest` link it.
Pure mechanical file moves — no logic changes.

**Depends on:** PR 5 merged (pg_autoctl Makefile fully settled before adding common/).

### Files being moved (git rename — no content changes)

```
debian.c/h         env_utils.c/h      file_utils.c/h
ini_file.c/h       ini_implementation.c
ipaddr.c/h         lock_utils.c/h
parsing.c/h        pgctl.c/h          pgsetup.c/h
pgsql.c/h          pgtuning.c/h       pidfile.c/h
signals.c/h        string_utils.c/h   system_utils.c/h
```

### New files

- `src/bin/common/Makefile`
- `src/bin/common/Makefile.common`

### Modified files

- `src/bin/pg_autoctl/Makefile` — point to `../common/` for moved files,
  link `../common/libpgaf_common.a`
- `src/bin/Makefile` — add `common` as first build target
- `src/bin/pgaftest/Makefile` — link `../common/libpgaf_common.a`

### What to do

1. Create branch `common-library` off PR 5 (or off main once PRs 1–5 merge).

2. Cherry-pick `3d42128`:
   ```
   git cherry-pick 3d42128
   ```
   This commit IS the file moves; git detects renames automatically.
   But it also touches `pgaftest/Makefile` — that's OK since pgaftest build
   references are inert until PR 7 adds the pgaftest source files.

3. Cherry-pick `cabeb1e` (macOS build fix):
   ```
   git cherry-pick cabeb1e
   ```

4. Cherry-pick `b0d52e0` (dangling pointer warnings):
   ```
   git cherry-pick b0d52e0
   ```

5. Verify pg_autoctl still builds:
   ```
   make -C src/bin clean && make -C src/bin
   ```
   The pg_autoctl binary should compile with headers resolved from `../common`.

6. Check that `#include` paths in pg_autoctl source still resolve (they use
   bare names like `#include "file_utils.h"` — the Makefile `-I ../common` flag
   makes this work).

### Rebase risk

The `src/bin/pg_autoctl/Makefile` is heavily modified by PRs 3–5.
Rebase `common-library` on top of the merged PRs, resolve Makefile conflicts once.
The moved files themselves will not conflict (git rename detection is reliable here).

### PR description

"Move generic utility sources to src/bin/common/ static library. Both
`pg_autoctl` and `pgaftest` link against it. No logic changes — pure file
reorganisation to enable the pgaftest binary (PR 7) without duplicating sources."

---

## PR 7 — pgaftest binary, DSL, test suite, upgrade test, CI workflow

**Goal:** Everything pgaftest. The new `pgaftest` binary with its bison/flex DSL,
22 `.pgaf` spec files covering all Python test scenarios, the live-upgrade test
with the dual-binary Docker image, and the `run-pgaftest.yml` CI workflow.

**Depends on:** PR 1 (CI workflow extends updated run-tests.yml) + PR 6 (pgaftest
links against src/bin/common/).

### New files

**Binary source** (`src/bin/pgaftest/`):
- `main.c`, `cli_root.c` — pgaftest CLI dispatch
- `test_spec.h` — AST node structs
- `test_spec_scan.l` — Flex lexer
- `test_spec_parse.y` — Bison grammar (pre-generated `.c/.h` files committed)
- `test_spec_parse.c/h`, `test_spec_scan.c` — committed bison/flex output
- `test_runner.c/h` — run/setup/step/down logic, TAP output, LISTEN loop
- `compose_gen.c/h` — Docker Compose YAML generator from cluster{} block
- `cli_demo.c/h` — demo app (moved from pg_autoctl do demo)
- `Makefile`

**Spec files** (`tests/tap/specs/`): 22 `.pgaf` files + `tests/tap/schedule`

**Upgrade test** (`tests/upgrade/`):
- `Dockerfile.current` — dual-binary image (v2.1 + v2.2)
- `Makefile` — `pgaf-current` and `pgaf-next` targets
- `install-extension.sh` — extension file installer baked into image
- `pg_autoctl_shim.sh` — translates `do service` → `internal service` after symlink flip

**CI workflow**: `.github/workflows/run-pgaftest.yml`

**Dockerfile**: add `pgaftest` and `debian` multi-stage targets, guard bison touch step

**Docs**: `docs/pgaftest.rst` (1 071 lines), `docs/ref/manual.rst` update

### What to do

1. Create branch `pgaftest` off PR 6 (or off main once PRs 1–6 merge).

2. The cleanest approach: take the full pgaftest-infra branch, then remove
   everything that belongs in PRs 1–6, leaving only pgaftest-specific changes:
   ```
   git checkout pgaftest-infra
   git rebase -i main   # squash all 48 commits into ~8 thematic ones
   ```
   Then rebase the result onto PR 6.

3. **Squash strategy** — 8 clean commits for this PR:
   - `pgaftest: build system and Makefile` — Makefile + common/ link
   - `pgaftest: DSL — lexer, parser, AST` — .l, .y, generated .c/.h, test_spec.h
   - `pgaftest: runner and CLI` — test_runner.c, cli_root.c, compose_gen.c, main.c
   - `pgaftest: spec suite` — all 22 .pgaf files + schedule
   - `pgaftest: upgrade test` — tests/upgrade/ additions
   - `pgaftest: Dockerfile targets` — pgaftest + debian stages, bison touch guard
   - `pgaftest: GitHub Actions workflow` — run-pgaftest.yml
   - `pgaftest: docs` — pgaftest.rst, manual.rst

4. **Bison touch guard** (important): the Dockerfile must touch pre-generated
   files BEFORE `make` so bison doesn't regenerate them (CI bison version may differ).
   Current correct form:
   ```dockerfile
   RUN if [ -d src/bin/pgaftest ]; then \
         touch src/bin/pgaftest/test_spec_parse.c \
               src/bin/pgaftest/test_spec_parse.h \
               src/bin/pgaftest/test_spec_scan.c; \
       fi
   ```
   The `-d` guard is needed because the upgrade test builds from a `git archive v2.1`
   that does not have `src/bin/pgaftest/` at all.

5. **Upgrade test image build**: before the CI run, the upgrade images must be built:
   ```
   make -C tests/upgrade pgaf-current   # builds pgaf:current (v2.1 + v2.2 binaries)
   make -C tests/upgrade pgaf-next      # builds pgaf:next (current branch)
   ```
   This is automated in the `run-pgaftest.yml` workflow under the `upgrade` job.

6. Verify the full pgaftest suite locally (PG17 default):
   ```
   make pgaftest-image   # build pgaftest Docker image
   pgaftest run tests/tap/specs/basic_operation.pgaf
   pgaftest run tests/tap/specs/multi_async.pgaf
   ```

7. CI target: `pgaftest workflow` should show 22/22 green. The known pre-existing
   failures (PG18, flaky Python tests) are in `Run Tests`, not here.

### Key bugs already fixed (do NOT regress)

These are in the current `pgaftest-infra` branch and must be carried into PR 7:
- `c3ae322` — trust notify convergence without subprocess double-check (multi_async fix)
- `5aba400` — poll monitor directly when LISTEN notifications missed (upgrade fix)
- `2185732` / `518d38f` — Dockerfile bison touch explicit paths + guard
- `7c093af` — SIGHUP race (already in PR 2; don't duplicate, just ensure it's in main first)

### PR description

"Add pgaftest: a new test binary and `.pgaf` spec language for deterministic
cluster testing. Replaces the Python nose test suite for CI, while also supporting
interactive cluster setup (`pgaftest setup spec.pgaf`).

- 22 spec files covering all current Python test scenarios
- Live upgrade test: binary + extension swap without container restart
- GitHub Actions workflow: 22 parallel jobs, each spec in its own Docker Compose network
- TAP output: compatible with `prove` and standard CI TAP consumers"

---

## Merge order summary

```
main ──┬── PR 1 (CI)       ──────────────────────────────────────────┐
       └── PR 2 (bug fixes)                                           │
            └── PR 3 (remove azure) ─────────────────────────────────┤
                  └── PR 4 (commands) ─────────────────────────────── ┤
                        └── PR 5 (node) ───────────────────────────── ┤
                              └── PR 6 (common/) ─────────────────────┤
                                          └── PR 7 (pgaftest) ◄───────┘
                                              (also needs PR 1)
```

PRs 1 and 2 have no dependencies and can be opened simultaneously.
PR 3 can open once PR 1's style check passes (or in parallel — no file overlap).
PRs 4 → 5 → 6 → 7 are a linear chain.
