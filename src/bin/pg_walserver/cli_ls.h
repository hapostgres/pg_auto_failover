/*
 * src/bin/pg_walserver/cli_ls.h
 *   `pg_walserver ls --pgdata <path> [--config]`: pg_walserver's own
 *   on-disk footprint under --pgdata, one inventory listing. A different
 *   axis from every "list" sub-command (cli_list.h): this is never about
 *   the archived WAL/base backup data itself, only pg_walserver's own
 *   small configuration/bookkeeping footprint.
 *
 *   Two tiers, because most of that footprint is not actually
 *   interesting to look at day to day: the config/credential/certificate
 *   files (pg_walserver.ini, pg_walserver_hba.conf, pg_walserver_passwd,
 *   server.crt/server.key, ca.crt) are written once, by the operator or
 *   by "setup"/"create-cert", and rarely change -- confirming they still
 *   exist tells an operator little. The runtime files (pg_walserver.pid,
 *   the ps state file) are the ones "serve" itself keeps current while
 *   running, and are what's actually worth a glance. Default output is
 *   the runtime tier only; --config adds the other one.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_LS_H
#define WS_CLI_LS_H

#include <stdbool.h>

/*
 * cli_ls_run prints one row per well-known pg_walserver bookkeeping file
 * under pgdata: whether it exists, its size, and its last-modified time.
 * The runtime tier (pg_walserver.pid, the ps state file) always prints;
 * the config/credential/certificate tier only prints when
 * includeConfigFiles is true (--config). Always returns true (a missing
 * file is an ordinary row, "exists: no", never an error -- an operator
 * running this against a freshly created, not-yet-configured --pgdata is
 * the common case, not a failure).
 */
bool cli_ls_run(const char *pgdata, bool includeConfigFiles);

#endif /* WS_CLI_LS_H */
