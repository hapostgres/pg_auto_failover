/*
 * src/bin/pg_walserver/clusters.h
 *   The server's cluster table: a small INI file, one section per cluster
 *   this instance serves, mapping the incoming connection's dbname to that
 *   cluster's own local storage root (who may connect is decided by hba.h,
 *   not here).
 *
 *   The section name -- the "cluster key" -- is an entirely opaque string as
 *   far as pg_walserver is concerned: it is never parsed, split, or given
 *   any filesystem meaning of its own (see clusters.c's own comment on
 *   clusters_find() for why a key that LOOKS like a path, such as
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
 *   One special key, "*" (WS_CLUSTERS_WILDCARD_KEY), is a catch-all fallback
 *   for any dbname that has no cluster of its own -- see clusters_find()'s own
 *   comment; the syntax and precedence are deliberately modelled on
 *   PgBouncer's own [databases] "*" entry (pgbouncer.org/config.html),
 *   since anyone who has run a PgBouncer already knows exactly what to
 *   expect from it here.
 *
 *   Deliberately just a path: which base backup is current, this cluster's
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

#ifndef WS_CLUSTERS_H
#define WS_CLUSTERS_H

#include <stdbool.h>

#include "postgres_fe.h"

#include "pgsql.h"

/* PgBouncer-style catch-all key, see clusters_find()'s own comment */
#define WS_CLUSTERS_WILDCARD_KEY "*"

typedef struct WsCluster
{
	char key[NAMEDATALEN + 16];         /* the cluster key exactly as it appears
	                                     * in the ini file (matched against
	                                     * dbname), or WS_CLUSTERS_WILDCARD_KEY */
	char path[MAXPGPATH];               /* this cluster's own local storage
	                                     * root -- WAL cache, basebackups/,
	                                     * and pg_walserver_systemid all live
	                                     * directly under it */
	char upstream[MAXCONNINFO];         /* optional: a libpq connection string
	                                     * to the instance this cluster archives
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
	                                      * cluster -- see clusters_find_by_
	                                      * hostname()'s own comment for why
	                                      * this exists (a real physical
	                                      * standby's dbname is always
	                                      * "replication", never a cluster
	                                      * key). Empty when the cluster is
	                                      * only ever reached by dbname. */
	bool receivewalPull;                /* "receivewal = pull" in this cluster's
	                                     * own section: opts it into
	                                     * pg_walserver's embedded WAL
	                                     * receivewal worker (receivewal.c) -- a
	                                     * supervised child running the
	                                     * vendored pg_receivewal against
	                                     * "upstream", writing straight into
	                                     * "path". Absent (false): the cluster
	                                     * is archive_command-push-only, or
	                                     * fed by something else entirely
	                                     * (an external pg_receivewal, or
	                                     * the pgaf-integrated pg_autoctl
	                                     * capturer) -- pg_walserver does not
	                                     * care which; ARCHIVE_FILE/
	                                     * CHECK_FILE are always reachable
	                                     * for any cluster regardless of this
	                                     * flag, gated purely by archiver-
	                                     * hba.conf like every other
	                                     * command. Written explicitly by
	                                     * "pg_walserver setup" by default
	                                     * now (opt out with --no-receivewal);
	                                     * see README.md's "The clusters file
	                                     * (pg_walserver.ini)" and "The
	                                     * embedded receivewal worker" sections
	                                     * for the full rationale. */
	bool disabled;                      /* "disabled = true" in this cluster's
	                                     * own section: "cluster drop" without
	                                     * --purge sets this instead of
	                                     * removing the section outright, so
	                                     * the cluster's own on-disk data is
	                                     * never orphaned (its "path" stays
	                                     * on record for a later "cluster
	                                     * drop --purge"/"cluster prune" to
	                                     * find and remove). A disabled cluster
	                                     * is otherwise inert: reload stops
	                                     * its embedded receivewal worker if
	                                     * one is running and never starts a
	                                     * new one, ws_authenticate() (auth.c)
	                                     * refuses every connection routed to
	                                     * it (CHECK_FILE/ARCHIVE_FILE/
	                                     * START_REPLICATION/archive-wal/
	                                     * restore-wal alike), and cli_
	                                     * resolve_upstream() (cli_upstream.c)
	                                     * refuses it for basebackup/fetch-
	                                     * systemid/set-upstream. "cluster
	                                     * enable" clears it again -- no
	                                     * connection URI to re-supply, since
	                                     * "path"/"upstream"/"hostname" are
	                                     * already on file (see cli_cluster.c's
	                                     * own ws_cluster_enable_run());
	                                     * "cluster register" on the same
	                                     * key/path clears it too, as a side
	                                     * effect (see write_cluster_section()),
	                                     * but needs the connection URI given
	                                     * again to do so. */
} WsCluster;

/*
 * clusters_load parses the clusters file at path into a freshly malloc'ed
 * array. Returns true with *clustersOut and *countOut set (possibly count
 * == 0, for an empty file or one that does not exist yet -- a normal,
 * expected state, never an error) on success, false on a malformed file
 * that does exist.
 */
bool clusters_load(const char *path, WsCluster **clustersOut, int *countOut);
void clusters_free(WsCluster *clusters);

/*
 * clusters_find resolves key (a dbname) to a cluster: an exact match if one
 * exists, else the "*" wildcard if the file has one, else NULL. The
 * ordinary, dbname-only lookup every command except auth.c's own
 * connection-dispatch decision wants -- see clusters_find_exact() and
 * clusters_find_by_hostname() for the two lower-level pieces auth.c
 * combines with a TLS SNI hostname in between these two tiers.
 */
const WsCluster * clusters_find(const WsCluster *clusters, int count, const char *key);

/* clusters_find() without the wildcard fallback: an exact key match, or NULL */
const WsCluster * clusters_find_exact(const WsCluster *clusters, int count, const
									  char *key);

/*
 * clusters_find_by_hostname resolves a TLS SNI hostname (case-insensitively,
 * as DNS names compare) to the one cluster whose own "hostname" property
 * matches it, or NULL when none does or hostname is NULL/empty. See
 * README.md's "Addressing a cluster beyond dbname: TLS SNI" section for why this
 * exists at all: a real physical standby's replication connection
 * always sends the literal dbname "replication", never a real cluster key,
 * so a cluster meant to be reachable *by name* by one needs a different
 * signal than dbname -- SNI, read before a single byte of the Postgres
 * protocol itself is exchanged, is unaffected by that override.
 */
const WsCluster * clusters_find_by_hostname(const WsCluster *clusters, int count,
											const char *hostname);

void clusters_slot_name(const char *clusterKey, char *out, size_t outSize);

/*
 * clusters_persist_path writes "path = <path>" into an *existing* [clusterKey]
 * section of the clusters file at clustersPath that doesn't have one yet (e.g.
 * a section an operator hand-wrote with only "upstream", or one predating
 * "path" defaulting to "<pgdata>/<clusterKey>" -- see cli_resolve_upstream()'s
 * own comment). Never creates a new section (that's "pg_walserver setup"'s
 * job, with its own upstream/TLS/receivewal handling); a no-op, returning
 * true, if the section already has a "path" property. false on any I/O or
 * parse failure, already logged.
 */
bool clusters_persist_path(const char *clustersPath, const char *clusterKey,
						   const char *path);

/*
 * clusters_set_property sets "propName = propValue" in an existing
 * [clusterKey] section, replacing that property's own line if the section
 * already has one, appending it otherwise -- always writes the given
 * value, unlike clusters_persist_path() above; "pg_walserver cluster
 * set-upstream"'s own way to change an already-registered cluster's
 * "upstream". Never creates a new section. false, with an error already
 * logged, if clusterKey has no section.
 */
bool clusters_set_property(const char *clustersPath, const char *clusterKey,
						   const char *propName, const char *propValue);

/*
 * clusters_drop_section removes the whole [clusterKey] section from the
 * clusters file at clustersPath -- "pg_walserver cluster drop"'s own job.
 * Never touches anything under the cluster's own "path" on disk (a
 * separate, explicit --purge decision, cli_root.c's own cluster-drop
 * command). false, with an error already logged, if clusterKey has no
 * section.
 */
bool clusters_drop_section(const char *clustersPath, const char *clusterKey);

/*
 * WsGlobalConfig is pg_walserver's own instance-level settings -- written
 * by "pg_walserver setup" as plain "key = value" lines at the very top of
 * pg_walserver.ini, before any cluster's own [section] header (the ini
 * library's own true anonymous/global section, INI_GLOBAL_SECTION;
 * clusters_load() already skips it when enumerating clusters, see its own
 * comment). "serve" reads this once at startup (and again on reload) to
 * fill in --port/--ssl-cert-file/--ssl-key-file/--ssl-ca-file/--auth-
 * timeout whenever the equivalent command-line flag wasn't given --
 * matches every other "explicit flag always wins over a persisted
 * default" precedent in this project. Never clusters -- those are
 * "pg_walserver register cluster"'s own job.
 */
typedef struct WsGlobalConfig
{
	bool havePort;
	int port;
	char sslCertFile[MAXPGPATH];
	char sslKeyFile[MAXPGPATH];
	char sslCaFile[MAXPGPATH];
	bool haveAuthTimeout;
	int authTimeout;
} WsGlobalConfig;

bool config_load_global(const char *configPath, WsGlobalConfig *out);

bool config_set_global_property(const char *configPath, const char *propName,
								const char *propValue);

/* the environment variable config_file_path() checks, see its own comment */
#define WS_CONFIG_FILE_ENV_VAR "PG_WALSERVER_CONFIG_FILE"

void config_file_path(const char *pgdata, const char *configFile,
					  char *out, size_t outSize);

#endif /* WS_CLUSTERS_H */
