/*
 * src/bin/pg_walserver/cli_ls.h
 *   `pg_walserver ls --pgdata <path> [--config <path>] [--all]`: a
 *   per-cluster storage summary -- how many base backups a cluster holds
 *   and their combined real size, how many WAL segments it has
 *   captured/archived and their combined size, and when its most recent
 *   base backup was taken.
 *
 *   Real PostgreSQL archiving practice (the "Continuous Archiving and
 *   Point-in-Time Recovery" chapter's own guidance on archive_cleanup_
 *   command and base backup frequency) treats an archive's disk footprint
 *   as something that needs active watching: the WAL archive grows
 *   without bound until something prunes it, and how far behind the most
 *   recent base backup is directly bounds how long a restore's WAL replay
 *   takes. Those are exactly the two questions an operator glancing at
 *   this command wants answered -- not "does pg_walserver_hba.conf still
 *   exist", which is what an earlier version of this command showed by
 *   default (see this file's own git history): a file written once by
 *   "setup" that essentially never needs checking again. That
 *   config/credential/certificate tier (pg_walserver.ini,
 *   pg_walserver_hba.conf, pg_walserver_passwd, server.crt/server.key,
 *   ca.crt) still has its place -- confirming a fresh deployment actually
 *   wrote what it should -- so it's still available, just behind
 *   --all, never the default anymore.
 *
 *   A different axis from every "list" sub-command (cli_list.h): "list
 *   backups"/"list wal" enumerate every individual backup/WAL file across
 *   every cluster, one row each; this command aggregates that same real
 *   data (reusing the exact same scan/enumeration code -- see cli_ls.c's
 *   own comment) into one row per cluster, the "how much, and how current"
 *   summary a human actually wants at a glance, not the full inventory.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_LS_H
#define WS_CLI_LS_H

#include <stdbool.h>

#include "commandline.h"

bool ws_ls_report(const char *pgdata, const char *configFile,
				  bool includeConfigFiles);

extern CommandLine ls_command;

#endif /* WS_CLI_LS_H */
