/*
 * src/bin/pg_walserver/cli_cluster.h
 *   `pg_walserver cluster register|drop|list|set-upstream`: the wizard
 *   that creates, removes, lists, and re-points the clusters (routes) one
 *   pg_walserver instance archives -- see cli_cluster.c for the full
 *   sequence each verb runs. Split out of what used to be "pg_walserver
 *   setup" (now cli_setup.h, narrowed to configuring pg_walserver itself,
 *   nothing about any one cluster): "setup" configures the server,
 *   "cluster" configures what it serves.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_CLUSTER_H
#define WS_CLI_CLUSTER_H

#include <stdbool.h>

#include "postgres_fe.h"

#include "pgsql.h"

typedef struct WsClusterRegisterOptions
{
	char pgdata[MAXPGPATH];
	char route[NAMEDATALEN + 16];
	char path[MAXPGPATH];
	char upstream[MAXCONNINFO];
	char host[_POSIX_HOST_NAME_MAX];
	char port[16];
	char user[NAMEDATALEN];
	char hostname[_POSIX_HOST_NAME_MAX]; /* the route's own TLS SNI hostname,
	                                      * written into pg_walserver.ini's
	                                      * "hostname" property -- see
	                                      * cli_cluster.c's own comment on
	                                      * why this matters the moment a
	                                      * second route is added */
	bool receivewalPull;                /* on by default (an operator has to
	                                     * pass --no-receivewal, or --receivewal
	                                     * none, to opt out): written as an
	                                     * explicit "receivewal = pull" into
	                                     * the route's own section
	                                     * (routes.h) unless opted out,
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

/*
 * ws_cluster_register_run runs the whole "cluster register" sequence
 * documented in cli_cluster.c's own header comment: validate/write the
 * pg_walserver.ini section, check the role's REPLICATION attribute, and
 * fetch the system identifier. It never takes a base backup itself:
 * "pg_walserver serve" bootstraps the route's first base backup
 * automatically, once, the next time it starts or reloads. Returns true
 * on success, false with an error already logged otherwise.
 */
bool ws_cluster_register_run(const WsClusterRegisterOptions *options);

/*
 * ws_cluster_drop_run removes routeKey's own registration from
 * pg_walserver.ini; with purge, also removes its own on-disk data
 * (everything under its own "path") -- see cli_cluster.c's own comment.
 * Returns true on success, false with an error already logged otherwise.
 */
bool ws_cluster_drop_run(const char *pgdata, const char *routeKey, bool purge);

/*
 * ws_cluster_list_run prints one row per registered route -- see
 * cli_cluster.c's own comment for exactly which fields, and how this
 * differs from :ref:`pg_walserver_list`'s own "list clusters". Returns
 * true (having printed a clean "no clusters registered yet" message,
 * never an error) when there are none, false only on a genuine problem
 * (e.g. no --pgdata given, or an unparsable pg_walserver.ini).
 */
bool ws_cluster_list_run(const char *pgdata);

/*
 * ws_cluster_set_upstream_run changes routeKey's own "upstream" property
 * to newUpstream, reloading an already-running "serve" for the same
 * --pgdata immediately afterward so its embedded receivewal worker (if
 * any) relocates onto the new upstream -- see cli_cluster.c's own
 * comment for the full rationale, including forceBasebackup's own
 * "take a fresh backup against the new upstream right away" behavior.
 * Returns true on success, false with an error already logged otherwise.
 */
bool ws_cluster_set_upstream_run(const char *pgdata, const char *routeKey,
								 const char *newUpstream,
								 bool forceBasebackup);

#endif /* WS_CLI_CLUSTER_H */
