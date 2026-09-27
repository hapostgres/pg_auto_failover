/*
 * src/bin/pg_walserver/cmd_show.h
 *   SHOW <name>: real pg_basebackup/pg_receivewal only ever query
 *   wal_segment_size and data_directory_mode (see streamutil.c in the
 *   Postgres source), so those are the only two GUCs this needs to answer.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_SHOW_H
#define WS_CMD_SHOW_H

#include "routes.h"

/* SHOW wal_segment_size answers the route's own size ("16MB", "64MB", "1GB") */
void cmd_show(int sock, const WsRoute *route, const char *name);

#endif /* WS_CMD_SHOW_H */
