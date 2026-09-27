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
	bool force;
	bool withBasebackup;
} WsSetupOptions;

/*
 * cli_setup_run runs the whole sequence documented in cli_setup.c's own
 * header comment: validate/write the pg_walserver.ini section, check the
 * role's REPLICATION attribute, fetch the system identifier, and -- only
 * with options->withBasebackup -- take the route's first base backup,
 * synchronously, returning only once it has actually succeeded (or
 * failed). Returns true on success, false with an error already logged
 * otherwise.
 */
bool cli_setup_run(const WsSetupOptions *options);

#endif /* WS_CLI_SETUP_H */
