/*
 * src/bin/pg_walserver/cmd_show.h
 *   SHOW <name>: real pg_basebackup/pg_receivewal only ever query
 *   wal_segment_size and data_directory_mode (see streamutil.c in the
 *   Postgres source); this project's own archive_command client
 *   (cli_archive.c) additionally queries "receivewal", this project's own
 *   extension with no PostgreSQL equivalent, to learn whether the
 *   connected cluster has an embedded receivewal worker before deciding whether
 *   to only ever CHECK_FILE or only ever ARCHIVE_FILE.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_SHOW_H
#define WS_CMD_SHOW_H

#include "clusters.h"

/*
 * SHOW wal_segment_size answers the cluster's own size ("16MB", "64MB", "1GB");
 * SHOW receivewal answers the cluster's own receivewal setting ("pull" or "none"),
 * read straight from WsCluster.receivewalPull.
 */
void cmd_show(int sock, const WsCluster *cluster, const char *name);

#endif /* WS_CMD_SHOW_H */
