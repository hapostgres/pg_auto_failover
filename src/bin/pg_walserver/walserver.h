/*
 * src/bin/pg_walserver/walsender.h
 *   Shared types for pg_walserver, the archiver's own replication-protocol
 *   server. Reimplements the wire-level surface of the real Postgres
 *   walsender well enough to serve IDENTIFY_SYSTEM, SHOW, and (not yet
 *   implemented) BASE_BACKUP/START_REPLICATION/TIMELINE_HISTORY to
 *   unmodified pg_basebackup/pg_receivewal clients, backed by an
 *   archiver's local WAL cache and base backups instead of a live
 *   postmaster. No frontend-linkable server-side protocol library exists
 *   anywhere in Postgres (confirmed against upstream PostgreSQL's
 *   pqcomm.c/backend_startup.c/repl_gram.y/walsender.c, all
 *   backend-only) -- this is a genuine reimplementation guided by that
 *   source, not a linking exercise.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_WALSERVER_H
#define WS_WALSERVER_H

#include <stdbool.h>

#include "postgres_fe.h"

/* one entry per route this instance serves, see routes.h */
typedef struct WsRoute WsRoute;

/*
 * Parsed StartupMessage contents we care about. "database" doubles as our
 * routing key: an opaque string matched against routes.ini/hba.conf, see
 * routes.h. pg_auto_failover's own archiver uses "<formation>/<group>"
 * (e.g. "default/0", as in its process title "pg_autoctl: walsender
 * default/0") -- one convention among any an operator could choose.
 */
typedef struct WsStartupParams
{
	char user[NAMEDATALEN];
	char database[NAMEDATALEN + 16];  /* the routing key, may exceed a bare
	                                   * NAMEDATALEN ("<formation>/<group>"
	                                   * can, see this struct's own comment) */
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

#endif /* WS_WALSERVER_H */
