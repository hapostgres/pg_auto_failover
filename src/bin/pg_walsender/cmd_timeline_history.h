/*
 * src/bin/pg_walsender/cmd_timeline_history.h
 *   TIMELINE_HISTORY <tli>: serves a "<tli>.history" file straight out of
 *   the route's WAL cache directory. Traced from walsender.c's own
 *   SendTimeLineHistory() (backend, not linked -- see walsender.h's own
 *   header comment): a single RowDescription(filename text, content text)
 *   + one DataRow + CommandComplete, no COPY involved. Genuinely just a
 *   flat-file read; the only real-instance-shaped input is which timeline
 *   was asked for.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_TIMELINE_HISTORY_H
#define WS_CMD_TIMELINE_HISTORY_H

#include "routes.h"

void cmd_timeline_history(int sock, const WsRoute *route, int timeline);

#endif /* WS_CMD_TIMELINE_HISTORY_H */
