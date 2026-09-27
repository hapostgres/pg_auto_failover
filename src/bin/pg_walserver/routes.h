/*
 * src/bin/pg_walserver/routes.h
 *   The server's routing table: a small INI file, one section per route
 *   this instance serves, mapping the incoming connection's dbname to that
 *   route's own local storage root (who may connect is decided by hba.h,
 *   not here).
 *
 *   The section name -- the "route key" -- is an entirely opaque string as
 *   far as pg_walserver is concerned: it is never parsed, split, or given
 *   any filesystem meaning of its own (see routes.c's own comment on
 *   routes_find() for why a key that LOOKS like a path, such as
 *   pg_auto_failover's own "<formation>/<group>" convention, still never
 *   touches the filesystem through the key itself -- only through the
 *   section's explicit "path" property). Any string an operator finds
 *   convenient works just as well: a bare cluster name, a customer id, a
 *   UUID. This file's whole design started well before the archiver even
 *   existed (see pg_walserver_standalone.pgaf, which never mentions a
 *   "formation" or "group" anywhere), and pg_auto_failover is simply one
 *   driver of it, not a requirement it imposes: pg_autoctl's own archiver
 *   reconciler (service_archiver_reconciler.c) writes this file whenever a
 *   membership is added or removed, using its own "<formation>/<group>"
 *   naming convention for the keys it happens to choose -- exactly as a
 *   human editing this file by hand, or any other tool driving pg_walserver
 *   outside of pg_auto_failover entirely, is free to pick their own.
 *
 *   One special key, "*" (WS_ROUTES_WILDCARD_KEY), is a catch-all fallback
 *   for any dbname that has no route of its own -- see routes_find()'s own
 *   comment; the syntax and precedence are deliberately modelled on
 *   PgBouncer's own [databases] "*" entry (pgbouncer.org/config.html),
 *   since anyone who has run a PgBouncer already knows exactly what to
 *   expect from it here.
 *
 *   Deliberately just a path: which base backup is current, this route's
 *   system identifier, and the current WAL position are NOT carried here.
 *   Each command that needs one of those reads it fresh, straight from a
 *   small purpose-built file under that same path, at connection time --
 *   see cmd_base_backup.c's own basebackups/.latest and cmd_identify_
 *   system.c's own archiver-systemid for the two current examples. pg_
 *   walsender itself never talks to the monitor -- see
 *   archiving-details.rst's "Keeping local files current" section
 *   for the full rationale behind this split.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_ROUTES_H
#define WS_ROUTES_H

#include <stdbool.h>

#include "postgres_fe.h"

/* PgBouncer-style catch-all key, see routes_find()'s own comment */
#define WS_ROUTES_WILDCARD_KEY "*"

typedef struct WsRoute
{
	char key[NAMEDATALEN + 16];         /* the route key exactly as it appears
	                                     * in the ini file (matched against
	                                     * dbname), or WS_ROUTES_WILDCARD_KEY */
	char path[MAXPGPATH];               /* this route's own local storage
	                                     * root -- WAL cache, basebackups/,
	                                     * and archiver-systemid all live
	                                     * directly under it */
} WsRoute;

/*
 * routes_load parses the routes file at path into a freshly malloc'ed
 * array. Returns true with *routesOut and *countOut set (possibly count
 * == 0 for an empty file) on success, false on a missing/malformed file.
 */
bool routes_load(const char *path, WsRoute **routesOut, int *countOut);
void routes_free(WsRoute *routes);

const WsRoute * routes_find(const WsRoute *routes, int count, const char *key);

#endif /* WS_ROUTES_H */
