/*
 * src/bin/pg_walserver/cli_status.h
 *   `pg_walserver status --pgdata <path>`: a short, scannable *process*
 *   status dashboard, and only that -- running or not (a real liveness
 *   check, not just "the pidfile exists"), pid, uptime, receivewal workers
 *   running vs. configured, and any bootstrap backups still pending (an
 *   in-flight background process, not archive data). Deliberately never
 *   the archive data those processes maintain (backup/WAL counts, sizes,
 *   which cluster is which) -- that's :ref:`pg_walserver_ls`'s and
 *   :ref:`pg_walserver_list`'s own job; status answers "is this process
 *   (and its own child processes) alive and healthy", nothing else. Named
 *   "status", never "state": pg_auto_failover's own FSM states (SINGLE/
 *   PRIMARY/SECONDARY/...) are an already-heavily-used, completely
 *   different concept in this codebase, and reusing "state" here would be
 *   genuinely confusing -- see README.md's own note on this naming
 *   choice.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_STATUS_H
#define WS_CLI_STATUS_H

#include <stdbool.h>

#include "commandline.h"

/*
 * cli_status_run prints the one-line/short dashboard described above.
 * Always returns true unless pgdata itself is missing -- "serve" not
 * running is a normal, cleanly reported case, never an error. configFile,
 * when given, overrides where the config file itself lives, independent
 * of pgdata -- see config_file_path()'s own comment, routes.h.
 */
bool cli_status_run(const char *pgdata, const char *configFile);

extern CommandLine status_command;

#endif /* WS_CLI_STATUS_H */
