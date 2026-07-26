/*
 * src/bin/pg_autoctl/timeline_history.h
 *   Reads the local node's own timeline history (from pg_wal/<tli>.history, not
 *   over a replication connection) and encodes it for publishing to the
 *   monitor.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef TIMELINE_HISTORY_H
#define TIMELINE_HISTORY_H

#include <stdbool.h>
#include <stdint.h>

#include "pgsetup.h"
#include "pgsql.h"

bool keeper_fetch_local_timeline_history(PostgresSetup *pgSetup,
										 uint32_t currentTLI,
										 IdentifySystem *system);

char * timeline_history_to_json(IdentifySystem *system);

#endif /* TIMELINE_HISTORY_H */
