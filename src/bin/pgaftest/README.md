# pgaftest — implementation notes

This file documents internal implementation details for contributors working on
the `pgaftest` binary.  For user-facing documentation see
`docs/ref/pgaftest.rst` and `docs/testing.rst`.

## Docker-out-of-Docker (DooD) architecture

`pgaftest cluster setup` and `pgaftest tmux` generate a Compose file that
includes a `setup` service alongside the cluster nodes.  That service:

- runs as **root** — the Docker daemon socket at `/var/run/docker.sock` is
  always accessible by root regardless of the host socket GID, which differs
  between macOS Docker Desktop and Linux CI runners;
- sets `working_dir: /root` (root's home directory) so that `pgaftest` can
  read and write a local state file in a predictable, writable location
  without any extra volume mounts;
- mounts the spec file read-only at `/root/spec.pgaf`;
- bind-mounts the host Docker socket (`/var/run/docker.sock`) so that
  `docker compose exec` calls issued from inside the container target the
  host's Docker daemon and reach the sibling cluster containers;
- pre-sets `PGAFTEST_SPEC=/root/spec.pgaf`, `PGAFTEST_HOST_WORK_DIR`, and
  `PGAFTEST_IN_CONTAINER=1` in its environment so all container commands
  discover the spec file and Compose project without extra arguments, and so
  the binary knows it is running inside the container (used to pick the
  correct state-file location and to prefer direct libpq connections over
  `docker compose exec` for monitor queries).

In `tmux` mode the service runs `sleep infinity` to stay alive; the setup
block is driven by a separate `docker compose run` invocation.  In CI mode
(`pgaftest run`) the same `docker compose run setup` invocation runs
`pgaftest _setup_` to execute the `setup {}` block.

## Step state file

Container commands record progress in `/root/pgaftest.state`
(`$HOME/pgaftest.state` inside the setup container), a small JSON file that
tracks:

- `current` — index of the next step to run in the sequence;
- `last_step` — name of the most recently executed step;
- `last_ok` — whether that step succeeded.

On success `current` advances; on failure it stays pointing at the failed step
so the next bare `pgaftest step` retries it rather than skipping ahead.

The file lives in `/root` (root's home directory inside the setup container)
rather than in the host-side bind-mounted work directory, so it is always
writable without any dependency on bind-mount ownership mapping.  It persists
for the lifetime of the container.

When `pgaftest step` is run on the host (outside the container), the state
file is written to `$TMPDIR/pgaftest/<spec-name>/pgaftest.state` alongside
the generated compose files.

## Direct libpq fast-paths

Where possible, `pgaftest` uses a direct libpq connection to the monitor rather
than shelling out to `docker compose exec`.  This is faster and avoids spawning
a child process per query.  The following operations use direct libpq:

- polling node state during `wait until <node> state = <s>` (both the
  fast-path check and the periodic 5-second fallback inside the LISTEN loop);
- `sql monitor { ... }` commands in step bodies;
- `promote <node>` — calls `pgautofailover.perform_promotion()` directly;
- `perform failover` — calls `pgautofailover.perform_failover()` directly;
- `pgaftest show state` — when running inside the container, invokes
  `pg_autoctl show state` directly (it is in PATH and `PG_AUTOCTL_MONITOR` is
  set in the environment).

Docker exec is still used for non-monitor services (node `exec` commands,
`sql node1 { ... }`, network disconnect/connect, compose lifecycle).

## Building

```sh
# Build only the pgaftest binary and its pgaf:pgaftest Docker image
make build-pgaftest

# Force a rebuild even if make thinks nothing changed
make force-build-pgaftest

# Build everything (node images for all supported Postgres versions + pgaftest)
make build
```

Set `PGAF_BUILD=1` in the environment to make the generated `docker-compose.yml`
use inline `build:` stanzas instead of `image:` references.  This is useful
when iterating on the Dockerfile without tagging an intermediate image:

```sh
PGAF_BUILD=1 pgaftest tmux tests/tap/specs/basic_operation.pgaf
```

## Source layout

| File | Purpose |
|------|---------|
| `main.c` | Entry point; dispatches to CLI sub-commands |
| `cli_root.c` | Top-level command tree (`run`, `tmux`, `cluster`, `step`, `show`, …) |
| `compose_gen.c` | Generates `docker-compose.yml` and per-node `.ini` files from the parsed spec |
| `test_runner.c` | Executes step DSL commands against a live compose stack; manages the state file |
| `test_spec_parse.y` | Bison grammar for `.pgaf` spec files |
| `test_spec_scan.l` | Flex lexer for `.pgaf` spec files |
