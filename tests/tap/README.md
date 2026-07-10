# pg_auto_failover TAP Test Suite

Tests are written in the `.pgaf` spec DSL and run by the `pgaftest` binary.
Output is [TAP](https://testanything.org/) (Test Anything Protocol),
compatible with `prove` and any TAP harness.

## Prerequisites

- `pgaftest` built: `make -C src/bin`
- Docker with the Compose plugin (`docker compose version`)

## Running tests

### All tests (via schedule)

```sh
pgaftest run --schedule tests/tap/schedule
```

Runs every spec listed in `tests/tap/schedule` and emits TAP on stdout.
Exit code is 0 only when all tests pass.

With `prove` for a formatted summary:

```sh
pgaftest run --schedule tests/tap/schedule | prove --tap -
```

### A single spec

```sh
pgaftest run tests/tap/specs/basic_operation.pgaf
```

### Interactive mode (live cluster, shell access)

Brings up the cluster, runs `setup {}`, then drops you into a shell:

```sh
pgaftest setup tests/tap/specs/basic_operation.pgaf
```

Run individual steps against the live cluster:

```sh
pgaftest step stop_primary   tests/tap/specs/basic_operation.pgaf
pgaftest step check_failover tests/tap/specs/basic_operation.pgaf
```

Inspect the generated `docker-compose.yml` without starting anything:

```sh
pgaftest show tests/tap/specs/basic_operation.pgaf
```

Tear down when done:

```sh
pgaftest down tests/tap/specs/basic_operation.pgaf
```

## Spec file format

Each `.pgaf` file in `specs/` is a complete, self-contained test scenario.

```
cluster {
    monitor
    formation {
        node1
        node2  candidate-priority 0
    }
}

setup {
    wait until node1 state is primary   timeout 120s
    wait until node2 state is secondary timeout 120s
}

teardown {
    compose down
}

step stop_primary {
    compose kill node1
}

step check_failover {
    wait until node2 state is primary  timeout 90s
    assert node2 state is primary
}

sequence
    stop_primary
    check_failover
```

| Block | Purpose |
|---|---|
| `cluster {}` | Topology — drives `docker-compose.yml` generation |
| `setup {}` | Run after `compose up -d`, before the first step |
| `teardown {}` | Always run at end (CI); on-demand in interactive mode |
| `step name {}` | Named command block |
| `sequence` | Step execution order for `pgaftest run` |

### Commands inside blocks

**Process control**

| Command | Effect |
|---|---|
| `exec <svc> <cmd…>` | `docker compose exec -T <svc> <cmd>` — fails if exit ≠ 0 |
| `exec-fails <svc> <cmd…>` | Same but asserts non-zero exit |
| `compose down` | `docker compose down --volumes` |
| `compose start <svc>` | `docker compose start <svc>` |
| `compose stop <svc>` | `docker compose stop <svc>` |
| `compose kill <svc>` | `docker compose kill <svc>` (SIGKILL) |
| `stop postgres <node>` | Stop Postgres inside the container without stopping the keeper |
| `start postgres <node>` | Restart Postgres inside the container via the keeper |

**State and timing**

| Command | Effect |
|---|---|
| `wait until <node> state is <s> [timeout Ns]` | Poll monitor; fail on timeout |
| `wait until <node> assigned-state is <s> [timeout Ns]` | Poll assigned state |
| `wait until <node> stopped [timeout Ns]` | Wait until container exits |
| `wait until <s1>, <s2>, … [in group N] [timeout Ns]` | Formation-wide state convergence |
| `wait until <n1> state is <s1> and <n2> state is <s2> … [timeout Ns]` | Multi-node simultaneous wait |
| `assert <node> state is <s>` | Instant check; no polling |
| `assert <node> assigned-state is <s>` | Check assigned state column |
| `assert <node> stays <s> while { … }` | Assert state unchanged throughout body |
| `sleep Ns` | Wait N seconds |

**SQL and expectations**

| Command | Effect |
|---|---|
| `sql <svc> { SQL }` | Run SQL on service, capture output |
| `expect { text }` | Substring-match last `sql` output |
| `expect { { row } { row } }` | Tuple form: match `psql --tuples-only --no-align` rows |
| `expect error [SQLSTATE]` | Assert previous `sql` raised a SQL error |

**Network**

| Command | Effect |
|---|---|
| `network disconnect <node>` | `docker network disconnect` — simulate partition |
| `network connect <node>` | `docker network connect` — restore |

**Log inspection**

| Command | Effect |
|---|---|
| `logs <svc> contains "<pat>"` | Assert literal `<pat>` appears in container logs |
| `logs <svc> not contains "<pat>"` | Assert literal `<pat>` does NOT appear |
| `logs <svc> matches "<pat>"` | Assert PCRE pattern matches a log line (`grep -P`) |
| `logs <svc> not matches "<pat>"` | Assert PCRE pattern does NOT match any log line |

**Cluster lifecycle**

| Command | Effect |
|---|---|
| `promote <node>[, <node>, …]` | `pg_autoctl perform promotion` per node |
| `set monitor <svc>` | Switch runner's active monitor (for replace-monitor tests) |

### `%CIDR%` macro

The literal `%CIDR%` is expanded to the Docker network CIDR in any `exec`
or `exec-fails` argument.  Use it to scope HBA rules to the test network:

```
exec monitor  bash -c "echo 'host all all %CIDR% trust' >> $PGDATA/pg_hba.conf"
```

## Schedule file

`tests/tap/schedule` lists one spec *name* per line (no path, no `.pgaf`
extension).  Lines starting with `#` are comments.

```
basic_operation
multi_standbys
# citus_basic   # requires Citus image — set CITUS_CLUSTER=1
```

## Relationship to Python tests

Each `.pgaf` spec was ported from a Python integration test in `tests/`.
The original is recorded in the spec header:

```
# Predecessor: tests/test_basic_operation.py
```

The pgaf framework replaces the Python `pgautofailover_utils` subprocess
harness with declarative DSL commands backed by `docker compose`.  Node
creation, cluster initialization, and teardown are handled by the cluster
declaration and `compose up / down`; test steps focus purely on the
scenario logic.

## CI integration

```yaml
- name: Run pg_auto_failover tests
  run: |
    make -C src/bin pgaftest
    pgaftest run --schedule tests/tap/schedule
```

Or with `prove`:

```yaml
- run: pgaftest run --schedule tests/tap/schedule | prove --tap -
```

For the full DSL reference see [`docs/pgaftest.rst`](../../docs/pgaftest.rst).
