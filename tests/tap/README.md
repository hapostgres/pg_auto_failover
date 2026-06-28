# pg_auto_failover TAP Test Suite

Tests are written in the `.pgaf` spec DSL and run by the `pgaftest` binary. Output is [TAP](https://testanything.org/) (Test Anything Protocol), compatible with `prove` and any TAP harness.

## Prerequisites

- `pgaftest` built: `make -C src/bin`
- Docker with the Compose plugin (`docker compose version`)
- PostgreSQL client tools on `PATH`

## Running tests

### All tests (via schedule)

```sh
pgaftest run --schedule tests/tap/schedule
```

This runs every spec listed in `tests/tap/schedule` in order and emits TAP on stdout. Exit code is 0 only when all tests pass.

With `prove` for a formatted summary:

```sh
pgaftest run --schedule tests/tap/schedule | prove --tap -
```

### A single spec

```sh
pgaftest run tests/tap/specs/basic_operation.pgaf
```

### Interactive mode (live cluster, shell access)

Brings up the cluster defined in the spec, runs the `setup{}` block, then drops you into a shell with the cluster running:

```sh
pgaftest setup tests/tap/specs/basic_operation.pgaf
```

Run individual steps against the live cluster:

```sh
pgaftest step stop_primary
pgaftest step check_failover
```

Tear down when done:

```sh
pgaftest down
```

## Spec file format

Each `.pgaf` file in `specs/` describes a complete test scenario:

```
cluster {
    monitor
    node node1
    node node2  candidate-priority=0
}

setup {
    wait until node1 state = primary   timeout 60s
    wait until node2 state = secondary timeout 60s
}

teardown {
    compose down
}

step stop_primary {
    exec node1  pg_autoctl stop postgres
}

step check_failover {
    wait until node2 state = primary  timeout 90s
    assert node2 state = primary
}

sequence
    stop_primary
    check_failover
```

| Block | Purpose |
|---|---|
| `cluster {}` | Topology — drives `docker-compose.yml` generation |
| `setup {}` | Run after `compose up`, before the first step |
| `teardown {}` | Always run at end (CI); on-demand in interactive mode |
| `step name {}` | Named command block |
| `sequence` | Step execution order for CI (`pgaftest run`) |

### Commands inside blocks

| Command | Effect |
|---|---|
| `exec <node> <cmd…>` | `docker compose exec <node> <cmd>` |
| `wait until <node> state = <s> [timeout Ns]` | Poll monitor; fail on timeout |
| `assert <node> state = <s>` | Fail immediately if state doesn't match |
| `assert <node> assigned-state = <s>` | Check `assigned_state` column |
| `sql <node> { SQL }` | Run SQL on node, capture result |
| `expect { text }` | Compare last `sql` output |
| `network disconnect <node>` | `docker network disconnect` |
| `network connect <node>` | `docker network connect` |
| `sleep Ns` | Wait N seconds |
| `compose down` | `docker compose down --volumes` |

## Schedule file

`tests/tap/schedule` lists one spec name per line (no path, no extension). Lines starting with `#` are comments.

```
basic_operation
auth
multi_standbys
...
# citus_basic   # requires Citus image
```

## CI integration

```yaml
- name: Run pg_auto_failover tests
  run: |
    make -C src/bin
    pgaftest run --schedule tests/tap/schedule | tee tap.out
    grep -qE '^(not ok|Bail out)' tap.out && exit 1 || exit 0
```

Or use `prove`:

```yaml
- run: pgaftest run --schedule tests/tap/schedule | prove --tap -
```
