/*
 * src/bin/pg_walserver/cli_cluster.h
 *   `pg_walserver cluster register <name> ...`, `pg_walserver cluster drop
 *   <name> ...`, `pg_walserver cluster list ...`, `pg_walserver cluster
 *   set-upstream <name> ...`: the wizard that creates, removes, lists,
 *   and re-points the clusters (clusters) one pg_walserver instance archives
 *   -- see cli_cluster.c for the full sequence each verb runs. Split out
 *   of what used to be "pg_walserver setup" (now cli_setup.h, narrowed to
 *   configuring pg_walserver itself, nothing about any one cluster):
 *   "setup" configures the server, these configure what it serves.
 *
 *   The cluster's own name is always given positionally (the first
 *   non-option argument), never a "--cluster" flag -- "cluster register
 *   mycluster ..." reads the way an operator says it out loud, and this
 *   project already gives a positional name to other resources it names
 *   elsewhere (`pg_walserver restore-wal <filename> <destination-path>`).
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_CLUSTER_H
#define WS_CLI_CLUSTER_H

#include <stdbool.h>

#include "commandline.h"

#include "postgres_fe.h"

#include "pgsql.h"

typedef struct WsClusterRegisterOptions
{
	char pgdata[MAXPGPATH];
	char configFile[MAXPGPATH];  /* --config: empty means the default, see
	                              * config_file_path()'s own comment */
	char cluster[NAMEDATALEN + 16]; /* the positional <name> */
	char path[MAXPGPATH];
	char pguri[MAXCONNINFO];     /* --pguri: a libpq connection string to
	                              * this cluster's own upstream (named to
	                              * match pg_autoctl's own "pguri"
	                              * vocabulary for a Postgres connection
	                              * string, e.g. monitor_pguri) */
	char host[_POSIX_HOST_NAME_MAX];
	char port[16];
	char user[NAMEDATALEN];
	char hostname[_POSIX_HOST_NAME_MAX]; /* the cluster's own TLS SNI hostname,
	                                      * written into pg_walserver.ini's
	                                      * "hostname" property -- see
	                                      * cli_cluster.c's own comment on
	                                      * why this matters the moment a
	                                      * second cluster is added */
	bool receivewalPull;                /* on by default (an operator has to
	                                     * pass --no-receivewal, or --receivewal
	                                     * none, to opt out): written as an
	                                     * explicit "receivewal = pull" into
	                                     * the cluster's own section
	                                     * (clusters.h) unless opted out,
	                                     * opting it into the embedded
	                                     * receivewal worker (receivewal.c) once "serve"
	                                     * starts. */
	bool force;
	bool sslSelfSigned;                 /* --ssl-self-signed: create a
	                                    * self-signed certificate for
	                                    * --pgdata right away, the same
	                                    * "skip create-cert entirely"
	                                    * convenience pg_autoctl's own
	                                    * --ssl-self-signed already gives
	                                    * -- see cli_cluster.c's own
	                                    * ensure_tls_certificate(). */
} WsClusterRegisterOptions;

bool ws_cluster_register_run(const WsClusterRegisterOptions *options);

/*
 * ws_cluster_drop_run, without purge, marks clusterKey's own registration
 * disabled in the config file config_file_path() resolves for pgdata/
 * configFile, rather than removing it -- its own on-disk data is left in
 * place and its "path" stays on record for a later --purge to find; with
 * purge, removes the registration outright and its own on-disk data too
 * -- see cli_cluster.c's own comment for the full rationale. Returns true
 * on success, false with an error already logged otherwise.
 */
bool ws_cluster_drop_run(const char *pgdata, const char *configFile,
						 const char *clusterKey, bool purge);

/*
 * ws_cluster_enable_run clears clusterKey's own "disabled" property -- the
 * dedicated, symmetric counterpart to "cluster drop" (without --purge):
 * see cli_cluster.c's own comment for how this differs from re-running
 * "cluster register" to the same end. Returns true (having printed a
 * clean "already active" message, never an error) when the cluster was
 * already enabled, false only on a genuine problem (no such cluster, or a
 * write failure).
 */
bool ws_cluster_enable_run(const char *pgdata, const char *configFile,
						   const char *clusterKey);

/*
 * ws_cluster_prune_run purges every disabled ("dropped") cluster at once,
 * the "ala docker" bulk equivalent of "cluster drop --purge <name>"
 * applied to every cluster currently disabled -- see cli_cluster.c's own
 * comment. Always returns true; a per-cluster rmtree() failure is warned
 * about, not fatal to the rest.
 */
bool ws_cluster_prune_run(const char *pgdata, const char *configFile);

/*
 * ws_cluster_list_run prints one row per registered cluster -- see
 * cli_cluster.c's own comment for exactly which fields, and how this
 * differs from :ref:`pg_walserver_list`'s own "list clusters". By
 * default, only active (non-disabled) clusters are shown; with
 * showDisabled (--disabled), only dropped (disabled) ones are -- the two
 * views are deliberately never combined into one table, the same reason
 * "docker ps" (running only) and "docker ps -a" (stopped included) stay
 * distinct rather than one command growing a column for it. Returns true
 * (having printed a clean "none" message, never an error) when the
 * chosen view has nothing to show, false only on a genuine problem (e.g.
 * neither --pgdata nor --config given, or an unparsable config file).
 * UPSTREAM is a full connection string/URI, often much wider than every
 * other column combined -- skipped from the default table entirely, and
 * only with showUpstream (--upstream) does it print at all, pivoted into
 * one key: value block per cluster rather than widening the row.
 */
bool ws_cluster_list_run(const char *pgdata, const char *configFile,
						 bool showUpstream, bool showDisabled);

/*
 * ws_cluster_set_upstream_run changes clusterKey's own "upstream" property
 * to newUpstream, reloading an already-running "serve" for the same
 * --pgdata immediately afterward so its embedded receivewal worker (if
 * any) relocates onto the new upstream -- see cli_cluster.c's own
 * comment for the full rationale, including forceBasebackup's own
 * "take a fresh backup against the new upstream right away" behavior.
 * Returns true on success, false with an error already logged otherwise.
 */
bool ws_cluster_set_upstream_run(const char *pgdata, const char *configFile,
								 const char *clusterKey,
								 const char *newUpstream,
								 bool forceBasebackup);

/*
 * ws_cluster_reload_running_server reloads an already-running "pg_
 * walserver serve" for the same --pgdata, if one is running -- see
 * cli_cluster.c's own comment. A no-op (logged, not an error) when
 * nothing is running.
 */
void ws_cluster_reload_running_server(const char *pgdata);

extern CommandLine cluster_register_command;
extern CommandLine cluster_drop_command;
extern CommandLine cluster_enable_command;
extern CommandLine cluster_list_command;
extern CommandLine cluster_prune_command;
extern CommandLine cluster_set_upstream_command;
extern CommandLine cluster_commands;

#endif /* WS_CLI_CLUSTER_H */
