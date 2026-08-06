/*
 * src/bin/pg_walsender/cmd_show.h
 *   SHOW <name>: real pg_basebackup/pg_receivewal only ever query
 *   wal_segment_size and data_directory_mode (see streamutil.c in the
 *   Postgres source), so those are the only two GUCs this needs to answer.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_SHOW_H
#define WS_CMD_SHOW_H

void cmd_show(int sock, const char *name);

#endif /* WS_CMD_SHOW_H */
