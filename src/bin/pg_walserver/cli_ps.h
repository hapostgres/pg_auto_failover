/*
 * src/bin/pg_walserver/cli_ps.h
 *   `pg_walserver ps --pgdata <path>`: a process-level view of a running
 *   "serve" instance -- its own pid, every supervised embedded pull
 *   capturer child, and any in-flight one-time bootstrap base backup job.
 *   See cli_ps.c's own header comment for how a brand-new "ps" process
 *   learns any of this about a *different*, already-running process.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_PS_H
#define WS_CLI_PS_H

#include <stdbool.h>

/*
 * cli_ps_run prints "serve"'s own process-level status for --pgdata.
 * Returns true (having printed a clean "not running" message, never an
 * error) when no "serve" is currently running for this --pgdata at all;
 * false only on a genuine problem (e.g. no --pgdata given).
 */
bool cli_ps_run(const char *pgdata);

#endif /* WS_CLI_PS_H */
