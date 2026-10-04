/*
 * src/bin/pg_walserver/cli_ps.h
 *   `pg_walserver ps --pgdata <path>`: a process-level view of a running
 *   "serve" instance -- its own pid, every supervised embedded
 *   receivewal worker child, and any in-flight one-time bootstrap base backup job.
 *   See cli_ps.c's own header comment for how a brand-new "ps" process
 *   learns any of this about a *different*, already-running process.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_PS_H
#define WS_CLI_PS_H

#include <stdbool.h>

#include "commandline.h"

bool cli_ps_run(const char *pgdata);

extern CommandLine ps_command;

#endif /* WS_CLI_PS_H */
