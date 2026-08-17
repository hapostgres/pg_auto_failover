/*
 * src/bin/pg_walsender/cmd_replication_slot.h
 *   CREATE_REPLICATION_SLOT / READ_REPLICATION_SLOT, physical slots only
 *   (matching the design doc's own scope). A slot here is a bookkeeping
 *   marker file under the route's WAL cache directory -- not a real
 *   Postgres slot on a live server (there's no live server), and not yet
 *   wired into any WAL-retention enforcement (that's the prune/retention
 *   milestone's job, see prune_archiver_wal() in the SQL schema).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_REPLICATION_SLOT_H
#define WS_CMD_REPLICATION_SLOT_H

#include "routes.h"

void cmd_create_replication_slot(int sock, const WsRoute *route, const char *rawArgs);
void cmd_read_replication_slot(int sock, const WsRoute *route, const char *rawArgs);

#endif /* WS_CMD_REPLICATION_SLOT_H */
