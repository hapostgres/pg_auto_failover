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

## Porting failure-simulation semantics from the Python suite

The user-facing reference for which `.pgaf` command to use for which kind of
node failure is in `docs/ref/pgaftest.rst` under "Failure-simulation
semantics". This section is the underlying audit: how the old
`tests/*.py` / `tests/pgautofailover_utils.py` suite simulated node failures,
and where the `.pgaf` ports currently stand relative to that mechanism.

### Python mechanisms (`tests/pgautofailover_utils.py`)

| Method | Mechanism | Notes |
|---|---|---|
| `PGNode.stop_pg_autoctl()` / `PGAutoCtl.stop()` | `os.kill(pid, SIGTERM)` on the `pg_autoctl run` process | Despite the docstring ("Kills the keeper..."), this is graceful: `pg_autoctl`'s supervisor runs its normal shutdown sequence on SIGTERM. |
| `PGNode.stop_postgres()` | `pg_ctl -D <datadir> --wait --mode fast stop` (SIGINT to postmaster), retried up to 60× | Bypasses `pg_autoctl` entirely — the keeper stays up and will try to restart Postgres, which is why the retry loop exists (races against that restart). |
| `PGNode.fail()` | `stop_pg_autoctl()` then, if Postgres is still up, `stop_postgres()` | The suite's standard "simulate a node failure" call. Composite of the two graceful primitives above — **not** a hard crash, no SIGKILL involved anywhere. |
| `PGNode.ifdown()` / `.ifup()` | `pyroute2` NDB: veth interface administratively down/up | Genuine network partition — processes keep running, only reachability is cut. |

No Python test helper ever sends SIGKILL to a node's `pg_autoctl` or
`postgres` process — the hardest failure the old suite could inflict was
SIGTERM + a fast `pg_ctl stop`.

### `.pgaf` DSL equivalents

| `.pgaf` command | Mechanism | Closest Python equivalent |
|---|---|---|
| `compose stop <svc>` | `docker compose stop` → SIGTERM to container PID 1 (`pg_autoctl`) | `node.fail()` / `stop_pg_autoctl()` |
| `compose kill <svc>` | `docker compose kill` → immediate SIGKILL | none — stricter than anything in the Python suite; use only with a documented reason (see `multi_alternate.pgaf`'s header comment) |
| `stop postgres <node>` / `start postgres <node>` | `pg_autoctl manual service pgctl off/on` inside the container | Close in intent to `stop_postgres()`, but goes through `pg_autoctl` rather than calling `pg_ctl` directly, so it does not reproduce the restart race the Python retry loop was written around |
| `network disconnect <node>` / `network connect <node>` | Docker network disconnect/connect (+ static `--ip` on reconnect, see below) | `ifdown()` / `ifup()` |

### Audit findings (2026-07-19)

Checked every `.pgaf` spec with a `network disconnect`/`connect` or
`compose stop`/`kill` step against the Python call site it was ported from
(`.fail()`, `.stop_pg_autoctl()`, or `.ifdown()`/`.ifup()`). Two files had a
`.fail()` call ported to `network disconnect` (a partition) instead of
`compose stop` (a graceful process stop) — a real mismatch, not a style
choice — and both were fixed:

- **`basic_operation.pgaf` `test_010_fail_primary`**: was `network disconnect
  node1`, ported from `node1.fail()`. This duplicated the file's own
  dedicated partition test (`test_021`–`023`, correctly ported from
  `ifdown()`/`ifup()`), meaning the file had silently lost coverage of
  "primary stops gracefully → failover". Fixed to `compose stop node1` /
  `compose start node1`, matching `test_015_fail_secondary` (an identical
  Python `node1.fail()` call in the same file that was already ported
  correctly).
- **`multi_standbys.pgaf` `test_012_fail_primary` and
  `test_015_002_fail_two_standby_nodes`**: same mismatch. Notably,
  `test_014_002_fail_two_standby_nodes` — the structurally identical
  scenario earlier in the same file, differing only in formation config —
  already correctly used `compose stop`. Fixed both to `compose stop` /
  `compose start`, matching `test_014`.

Both fixes were verified with a full local run of the affected spec (27/27
and 27/27 steps passing) before being committed.

The following files also substitute `network disconnect` for a Python
`.fail()` call, but were left as-is: each uses the substitution
*consistently* for every "fail" step in the file (no internal
inconsistency, and no separately duplicated partition test), so the
divergence looks like a deliberate simplification rather than a bug —
flagging here as a lower-priority follow-up rather than fixing blind:
`citus_basic_operation.pgaf`, `citus_multi_standbys.pgaf`,
`multi_ifdown.pgaf` (test_008, node1). `citus_nonha_operation.pgaf` and
`citus_force_failover.pgaf` were checked and are already correct
(`compose stop` where the Python source used `.fail()`, `network
disconnect`/`connect` where it used `ifdown()`/`ifup()`).

## Deterministic node registration order

Postgres containers in a spec's `cluster{}` block are started by Docker
Compose largely in parallel (see `depends_on` below), so without
intervention, which node registers with the monitor first — and therefore
which node gets node id 1, becomes the initial primary of its group, etc. —
would be a race. Two mechanisms combine to make it deterministic instead:

1. **`depends_on` (`compose_gen.c`, `compose_gen_write()`).** The very first
   node declared anywhere in the spec (across every `formation{}` block, in
   declaration order) gets a Docker healthcheck (`pg_autoctl status`).
   Every other node's service depends on that first node reaching
   `service_healthy` before Compose will even start its container. This
   guarantees the first-declared node always registers before anything
   else — but all the *other* nodes still become eligible to start at the
   same moment once that healthcheck passes, so their relative order among
   themselves is still a race at that point.

2. **`PG_AUTOCTL_TEST_DELAY` (`compose_gen.c` + `cli_node.c`).** To break
   that remaining race, `compose_gen_write()` assigns each node a 0-based
   ordinal — its position in `cluster{}` declaration order across every
   formation, the same order used to pick the first node above — and
   writes it as `PG_AUTOCTL_TEST_DELAY: "<ordinal>"` in that node's
   container environment. On a cold start (no existing config file yet),
   `pg_autoctl node run` (`cli_node.c`, `cli_node_run()`) reads that value
   and sleeps `2 * ordinal` seconds before registering with the monitor.
   The first node (ordinal 0) doesn't sleep at all; each node after it
   waits 2 seconds longer than the one before, so even though their
   containers all start around the same time, they register with the
   monitor strictly in declaration order.

   The ordinal is computed once in `compose_gen.c` and handed to the
   container as a plain number — `cli_node.c` never parses the node's name
   to figure out its position. That's a deliberate design choice: an
   earlier version derived the delay from a numeric suffix on the node's
   name (`node1` → 2s, `node2` → 4s, …), which broke down for Citus-style
   names like `worker1a` or `coordinator1b` that don't end in a digit at
   all — those got zero delay under the old scheme. Ordinal-based delay
   works identically for any naming convention, since it never looks at
   the name.

   Practical effect for Citus formations: nodes register in exactly the
   order they're declared in `cluster{}`, so (absent an explicit
   `promote`) the first node declared in each worker/coordinator group
   also becomes that group's initial primary — matching what
   `citus_basic_operation.pgaf` and friends already assume when they write
   `formation { coordinator1a coordinator; coordinator1b coordinator;
   worker1a worker group 1; worker1b worker group 1; … }` and then
   `promote coordinator1a; promote worker1a; …` in `setup{}` (those
   `promote` calls become no-ops confirming the already-registered
   topology, rather than doing any real work).

## Static IP on `network connect`

`docker network connect` without `--ip` hands the container a fresh DHCP
address. The dnsmasq `pgaf-hosts` file (which `pg_hba.conf` entries are
generated against) maps each node to a fixed address, so a reconnect that
picks a new IP breaks HBA matching — `runner_network_on()` in
`test_runner.c` reads the node's original address from `pgaf-hosts` and
passes it via `--ip` to avoid this.

## Source layout

| File | Purpose |
|------|---------|
| `main.c` | Entry point; dispatches to CLI sub-commands |
| `cli_root.c` | Top-level command tree (`run`, `tmux`, `cluster`, `step`, `show`, …) |
| `compose_gen.c` | Generates `docker-compose.yml` and per-node `.ini` files from the parsed spec |
| `test_runner.c` | Executes step DSL commands against a live compose stack; manages the state file |
| `test_spec_parse.y` | Bison grammar for `.pgaf` spec files |
| `test_spec_scan.l` | Flex lexer for `.pgaf` spec files |
