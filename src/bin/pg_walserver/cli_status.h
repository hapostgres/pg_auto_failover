/*
 * src/bin/pg_walserver/cli_status.h
 *   `pg_walserver status --pgdata <path>`: a short, scannable dashboard --
 *   running or not (a real liveness check, not just "the pidfile exists"),
 *   pid, cluster count, receivewal workers running vs. configured, and any
 *   bootstrap backups still pending. Named "status", never "state":
 *   pg_auto_failover's own FSM states (SINGLE/PRIMARY/SECONDARY/...) are
 *   an already-heavily-used, completely different concept in this
 *   codebase, and reusing "state" here would be genuinely confusing --
 *   see README.md's own note on this naming choice.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_STATUS_H
#define WS_CLI_STATUS_H

#include <stdbool.h>

/*
 * cli_status_run prints the one-line/short dashboard described above.
 * Always returns true unless pgdata itself is missing -- "serve" not
 * running is a normal, cleanly reported case, never an error.
 */
bool cli_status_run(const char *pgdata);

#endif /* WS_CLI_STATUS_H */
