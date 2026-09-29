/*
 * src/bin/pg_walserver/cli_list.h
 *   `pg_walserver list clusters|backups|wal`: three read-only inventory
 *   views over one or every route configured in a "--pgdata"'s own
 *   "pg_walserver.ini". See cli_list.c's own header comment for the LSN-
 *   range computation and per-invocation caching design; cli_root.c wires
 *   each of the three functions below into its own "list <name>"
 *   CommandLine, the same split every other pg_walserver sub-command in
 *   this codebase already uses (flag parsing and the CommandLine table
 *   stay in cli_root.c, the actual work stays in a dedicated cli_*.c).
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_LIST_H
#define WS_CLI_LIST_H

#include <stdbool.h>

/*
 * cli_list_clusters_run prints one row per route configured in the config
 * file config_file_path() resolves for pgdata/configFile (or just
 * clusterFilter's own route, when not NULL/empty): its path/upstream/
 * hostname/receivewal setting, whether it has a base backup, whether its
 * embedded receivewal worker is currently running (cross-referenced
 * against the ps state file, ps_state.h -- "n/a" when "serve" is not
 * running at all), and the WAL range it currently covers (start LSN from
 * its latest base backup's own backup_label, end LSN from the newest WAL
 * segment actually present). Returns false, with an error already
 * logged, only on a configuration problem (no such --pgdata/config file,
 * or no such cluster).
 */
bool cli_list_clusters_run(const char *pgdata, const char *configFile,
						   const char *clusterFilter);

/*
 * cli_list_backups_run prints one row per base backup found under every
 * matching route's own "basebackups/" directory (reusing cli_archive_
 * cleanup.c's own ws_backup_list_load()): its label/timestamp, size on
 * disk, and whether it is the route's ".latest".
 */
bool cli_list_backups_run(const char *pgdata, const char *configFile,
						  const char *clusterFilter);

/*
 * cli_list_wal_run prints, per matching route, aggregate WAL cache stats
 * (segment count, total bytes, oldest/newest segment, .history file count)
 * by default, or, with segments true, one row per individual WAL/.partial/
 * .backup/.history file instead.
 */
bool cli_list_wal_run(const char *pgdata, const char *configFile,
					  const char *clusterFilter, bool segments);

#endif /* WS_CLI_LIST_H */
