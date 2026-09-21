/*
 * src/bin/pg_walsender/cmd_replication_slot.h
 *   CREATE_REPLICATION_SLOT / READ_REPLICATION_SLOT, physical slots only
 *   (that is all that is supported). A slot here is a bookkeeping
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
void cmd_drop_replication_slot(int sock, const WsRoute *route, const char *rawArgs);

/*
 * Slot names are [a-z0-9_]{1,63}, as in PostgreSQL; a route holds at most 64
 * slots (a clear error beyond); a slot file is written atomically (temp +
 * rename). CREATE of an existing slot is an error (42710) and never resets
 * it.
 */

#endif /* WS_CMD_REPLICATION_SLOT_H */
