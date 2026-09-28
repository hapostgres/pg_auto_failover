/*
 * src/bin/pg_walserver/cli_setup.h
 *   `pg_walserver setup`: the wizard that creates or validates one
 *   pg_walserver.ini route from the command line -- see cli_setup.c for
 *   the full sequence.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_SETUP_H
#define WS_CLI_SETUP_H

#include <stdbool.h>

#include "postgres_fe.h"

#include "pgsql.h"

typedef struct WsSetupOptions
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
	                                      * cli_setup.c's own comment on why
	                                      * this matters the moment a second
	                                      * route is added */
	bool capturePull;                   /* on by default (an operator has to
	                                     * pass --no-capture, or --capture
	                                     * none, to opt out): written as an
	                                     * explicit "capture = pull" into
	                                     * the route's own section
	                                     * (routes.h) unless opted out,
	                                     * opting it into the embedded pull
	                                     * capturer (capture.c) once "serve"
	                                     * starts. Explicit --capture pull
	                                     * still works too, a no-op given
	                                     * the new default -- see
	                                     * cli_setup.c's own header comment
	                                     * for why setup writes the property
	                                     * explicitly rather than relying on
	                                     * a changed on-disk default (a
	                                     * route's own "capture" property is
	                                     * still simply absent == off for
	                                     * anyone hand-editing
	                                     * pg_walserver.ini directly;
	                                     * routes.c/routes.h are unchanged). */
	bool force;
} WsSetupOptions;

/*
 * cli_setup_run runs the whole sequence documented in cli_setup.c's own
 * header comment: validate/write the pg_walserver.ini section, check the
 * role's REPLICATION attribute, and fetch the system identifier. It never
 * takes a base backup itself: "pg_walserver serve" bootstraps the route's
 * first base backup automatically, once, the next time it starts or
 * reloads (see accept_loop.c's own ws_bootstrap_missing_backups()).
 * Returns true on success, false with an error already logged otherwise.
 */
bool cli_setup_run(const WsSetupOptions *options);

#endif /* WS_CLI_SETUP_H */
