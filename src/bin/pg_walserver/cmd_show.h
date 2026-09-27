/*
 * src/bin/pg_walserver/cmd_show.h
 *   SHOW <name>: real pg_basebackup/pg_receivewal only ever query
 *   wal_segment_size and data_directory_mode (see streamutil.c in the
 *   Postgres source); this project's own archive_command client
 *   (cli_archive.c) additionally queries "capture", this project's own
 *   extension with no PostgreSQL equivalent, to learn whether the
 *   connected route has an embedded pull capturer before deciding whether
 *   to only ever CHECK_FILE or only ever ARCHIVE_FILE.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_SHOW_H
#define WS_CMD_SHOW_H

#include "routes.h"

/*
 * SHOW wal_segment_size answers the route's own size ("16MB", "64MB", "1GB");
 * SHOW capture answers the route's own capture setting ("pull" or "none"),
 * read straight from WsRoute.capturePull.
 */
void cmd_show(int sock, const WsRoute *route, const char *name);

#endif /* WS_CMD_SHOW_H */
