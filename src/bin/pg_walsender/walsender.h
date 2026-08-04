/*
 * src/bin/pg_walsender/walsender.h
 *   Shared types for pg_walsender, the archiver's own replication-protocol
 *   server (see ~/dev/temp/archiving-disaster-recovery.md, "Process model"
 *   and "Build order" milestone 2). Reimplements the wire-level surface of
 *   the real Postgres walsender well enough to serve IDENTIFY_SYSTEM, SHOW,
 *   and (later milestones) BASE_BACKUP/START_REPLICATION/TIMELINE_HISTORY
 *   to unmodified pg_basebackup/pg_receivewal clients, backed by an
 *   archiver's local WAL cache and base backups instead of a live
 *   postmaster. No frontend-linkable server-side protocol library exists
 *   anywhere in Postgres (confirmed against
 *   /Users/dim/dev/PostgreSQL/postgresql's pqcomm.c/backend_startup.c/
 *   repl_gram.y/walsender.c, all backend-only) -- this is a genuine
 *   reimplementation guided by that source, not a linking exercise.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_WALSENDER_H
#define WS_WALSENDER_H

#include <stdbool.h>

#include "postgres_fe.h"

/* one entry per "<formation>/<group>" the archiver serves, see routes.h */
typedef struct WsRoute WsRoute;

/*
 * Parsed StartupMessage contents we care about. "database" doubles as our
 * routing key ("<formation>/<group>", see the design doc's own worked
 * process-title example, "pg_autoctl: walsender default/0").
 */
typedef struct WsStartupParams
{
	char user[NAMEDATALEN];
	char database[NAMEDATALEN + 16];  /* "<formation>/<group>", may exceed a bare NAMEDATALEN */
	char applicationName[NAMEDATALEN];
	bool replication;

	/*
	 * True only when the client's startup packet set replication=database
	 * (pg_basebackup's style) rather than a plain replication=1/true
	 * (pg_receivewal's style). IDENTIFY_SYSTEM's own dbname column must be
	 * NULL for the latter -- real pg_receivewal fatals out ("unexpectedly
	 * database specific") if it isn't, since a non-NULL dbname is its
	 * signal that the connection was accidentally database-qualified.
	 */
	bool replicationDatabase;
} WsStartupParams;

#endif /* WS_WALSENDER_H */
