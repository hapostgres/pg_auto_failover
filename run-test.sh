#!/usr/bin/env bash
# Run a pgaftest spec or schedule locally using Docker-out-of-Docker.
#
# Usage:
#   ./run-test.sh <spec.pgaf>                   run a single spec (PG17)
#   ./run-test.sh <spec.pgaf> [PGVERSION]        run a single spec
#   ./run-test.sh --schedule <name> [PGVERSION]  run a named schedule
#
# Multiple invocations can run in parallel — each spec gets its own
# subdirectory under /tmp/pgaftest and its own Docker Compose project.
#
# Examples:
#   ./run-test.sh tests/tap/specs/multi_ifdown.pgaf
#   ./run-test.sh tests/tap/specs/citus_skip_pg_hba.pgaf 17
#   ./run-test.sh --schedule multi-2
#   ./run-test.sh --schedule ssl 14 &
#   ./run-test.sh --schedule ssl 15 &

set -euo pipefail

PGVERSION=17
CMD=()

if [[ "${1:-}" == "--schedule" ]]; then
    SCHEDULE="${2:?Usage: $0 --schedule <name> [PGVERSION]}"
    PGVERSION="${3:-$PGVERSION}"
    CMD=(pgaftest run --schedule "tests/tap/schedules/${SCHEDULE}.sch")
else
    SPEC="${1:?Usage: $0 <spec.pgaf> [PGVERSION]}"
    PGVERSION="${2:-$PGVERSION}"
    CMD=(pgaftest run "$SPEC")
fi

mkdir -p /tmp/pgaftest

docker run --rm \
    --user root \
    -v /var/run/docker.sock:/var/run/docker.sock \
    -v /tmp/pgaftest:/tmp/pgaftest \
    -v "$(pwd)":/work:ro \
    -w /work \
    -e PGAF_IMAGE=pgaf:run \
    -e PGVERSION="$PGVERSION" \
    pgaf:pgaftest \
    "${CMD[@]}"
