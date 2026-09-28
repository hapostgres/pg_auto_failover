/*
 * src/bin/pg_walserver/cli_ls.h
 *   `pg_walserver ls --pgdata <path>`: pg_walserver's own on-disk
 *   footprint -- where its bookkeeping files (pg_walserver.ini,
 *   pg_walserver_hba.conf, pg_walserver_passwd, server.crt/server.key,
 *   ca.crt, pg_walserver.pid, the ps state file) actually are under
 *   --pgdata, one inventory listing. A different axis from every "list"
 *   sub-command (cli_list.h): this is never about the archived WAL/base
 *   backup data itself, only pg_walserver's own small configuration/
 *   bookkeeping footprint.
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
 * Always returns true (a missing file is an ordinary row, "exists: no",
 * never an error -- an operator running this against a freshly created,
 * not-yet-configured --pgdata is the common case, not a failure).
 */
bool cli_ls_run(const char *pgdata);

#endif /* WS_CLI_LS_H */
