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
 *   system.c's own pg_walserver_systemid for the two current examples. pg_
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

#include "pgsql.h"

/* PgBouncer-style catch-all key, see routes_find()'s own comment */
#define WS_ROUTES_WILDCARD_KEY "*"

typedef struct WsRoute
{
	char key[NAMEDATALEN + 16];         /* the route key exactly as it appears
	                                     * in the ini file (matched against
	                                     * dbname), or WS_ROUTES_WILDCARD_KEY */
	char path[MAXPGPATH];               /* this route's own local storage
	                                     * root -- WAL cache, basebackups/,
	                                     * and pg_walserver_systemid all live
	                                     * directly under it */
	char upstream[MAXCONNINFO];         /* optional: a libpq connection string
	                                     * to the instance this route archives
	                                     * from -- read as a default by
	                                     * fetch-systemid/basebackup/setup,
	                                     * always overridable by an explicit
	                                     * --upstream/--host/--port/--user
	                                     * flag. Empty when the ini section
	                                     * has no "upstream" property: those
	                                     * sub-commands then require the flag
	                                     * instead. Named "upstream", not
	                                     * "primary_conninfo" (misleading --
	                                     * the source may be a standby) or
	                                     * "source"/"target" (ambiguous about
	                                     * direction); "upstream" is also
	                                     * already PostgreSQL's own vocabulary
	                                     * for "the server this one replicates
	                                     * from" in cascading replication. */
	char hostname[_POSIX_HOST_NAME_MAX]; /* optional: the TLS SNI hostname a
	                                      * client presents to reach this
	                                      * route -- see routes_find_by_
	                                      * hostname()'s own comment for why
	                                      * this exists (a real physical
	                                      * standby's dbname is always
	                                      * "replication", never a route
	                                      * key). Empty when the route is
	                                      * only ever reached by dbname. */
	bool receivewalPull;                /* "receivewal = pull" in this route's
	                                     * own section: opts it into
	                                     * pg_walserver's embedded WAL
	                                     * receivewal worker (receivewal.c) -- a
	                                     * supervised child running the
	                                     * vendored pg_receivewal against
	                                     * "upstream", writing straight into
	                                     * "path". Absent (false): the route
	                                     * is archive_command-push-only, or
	                                     * fed by something else entirely
	                                     * (an external pg_receivewal, or
	                                     * the pgaf-integrated pg_autoctl
	                                     * capturer) -- pg_walserver does not
	                                     * care which; ARCHIVE_FILE/
	                                     * CHECK_FILE are always reachable
	                                     * for any route regardless of this
	                                     * flag, gated purely by archiver-
	                                     * hba.conf like every other
	                                     * command. Written explicitly by
	                                     * "pg_walserver setup" by default
	                                     * now (opt out with --no-receivewal);
	                                     * see README.md's "The routes file
	                                     * (pg_walserver.ini)" and "The
	                                     * embedded receivewal worker" sections
	                                     * for the full rationale. */
} WsRoute;

/*
 * routes_load parses the routes file at path into a freshly malloc'ed
 * array. Returns true with *routesOut and *countOut set (possibly count
 * == 0, for an empty file or one that does not exist yet -- a normal,
 * expected state, never an error) on success, false on a malformed file
 * that does exist.
 */
bool routes_load(const char *path, WsRoute **routesOut, int *countOut);
void routes_free(WsRoute *routes);

/*
 * routes_find resolves key (a dbname) to a route: an exact match if one
 * exists, else the "*" wildcard if the file has one, else NULL. The
 * ordinary, dbname-only lookup every command except auth.c's own
 * connection-routing decision wants -- see routes_find_exact() and
 * routes_find_by_hostname() for the two lower-level pieces auth.c
 * combines with a TLS SNI hostname in between these two tiers.
 */
const WsRoute * routes_find(const WsRoute *routes, int count, const char *key);

/* routes_find() without the wildcard fallback: an exact key match, or NULL */
const WsRoute * routes_find_exact(const WsRoute *routes, int count, const char *key);

/*
 * routes_find_by_hostname resolves a TLS SNI hostname (case-insensitively,
 * as DNS names compare) to the one route whose own "hostname" property
 * matches it, or NULL when none does or hostname is NULL/empty. See
 * README.md's "Routing beyond dbname: TLS SNI" section for why this
 * exists at all: a real physical standby's replication connection
 * always sends the literal dbname "replication", never a real route key,
 * so a route meant to be reachable *by name* by one needs a different
 * signal than dbname -- SNI, read before a single byte of the Postgres
 * protocol itself is exchanged, is unaffected by that override.
 */
const WsRoute * routes_find_by_hostname(const WsRoute *routes, int count,
										const char *hostname);

/*
 * routes_slot_name derives a valid, deterministic PostgreSQL replication
 * slot name (lowercase alnum/underscore only, NAMEDATALEN-1 bytes max) from
 * an arbitrary route key -- which, unlike a slot name, is an entirely
 * opaque string with no character restrictions (see this file's own header
 * comment: pg_auto_failover's own archiver reconciler uses
 * "<formation>/<group>" keys, for one). The sanitized key alone could
 * collide (e.g. "a/b" and "a-b" both sanitize to "a_b"); a short CRC32C
 * suffix of the *original*, unsanitized key makes every slot name unique
 * per route regardless. Always writes a NUL-terminated name into out
 * (truncating the sanitized part, never the suffix, if it would overflow
 * outSize/NAMEDATALEN).
 */
void routes_slot_name(const char *routeKey, char *out, size_t outSize);

/*
 * routes_persist_path writes "path = <path>" into an *existing* [routeKey]
 * section of the routes file at routesPath that doesn't have one yet (e.g.
 * a section an operator hand-wrote with only "upstream", or one predating
 * "path" defaulting to "<pgdata>/<routeKey>" -- see cli_resolve_upstream()'s
 * own comment). Never creates a new section (that's "pg_walserver setup"'s
 * job, with its own upstream/TLS/receivewal handling); a no-op, returning
 * true, if the section already has a "path" property. false on any I/O or
 * parse failure, already logged.
 */
bool routes_persist_path(const char *routesPath, const char *routeKey,
						 const char *path);

/*
 * routes_set_property sets "propName = propValue" in an existing
 * [routeKey] section, replacing that property's own line if the section
 * already has one, appending it otherwise -- always writes the given
 * value, unlike routes_persist_path() above; "pg_walserver cluster
 * set-upstream"'s own way to change an already-registered route's
 * "upstream". Never creates a new section. false, with an error already
 * logged, if routeKey has no section.
 */
bool routes_set_property(const char *routesPath, const char *routeKey,
						 const char *propName, const char *propValue);

/*
 * routes_drop_section removes the whole [routeKey] section from the
 * routes file at routesPath -- "pg_walserver cluster drop"'s own job.
 * Never touches anything under the route's own "path" on disk (a
 * separate, explicit --purge decision, cli_root.c's own cluster-drop
 * command). false, with an error already logged, if routeKey has no
 * section.
 */
bool routes_drop_section(const char *routesPath, const char *routeKey);

#endif /* WS_ROUTES_H */
