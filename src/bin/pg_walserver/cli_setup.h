/*
 * src/bin/pg_walserver/cli_setup.h
 *   `pg_walserver setup --pgdata <path> [--config-file <path>]
 *   [--port <port>] [--ssl-cert-file <path>] [--ssl-key-file <path>]
 *   [--ssl-ca-file <path>] [--auth-timeout <seconds>]`: configures
 *   pg_walserver *itself* -- writes whichever of these were given as
 *   plain "key = value" lines at the top of its own config file
 *   (config_set_global_property(), routes.c), so "pg_walserver serve
 *   --pgdata <path>" alone, with none of these flags repeated, already
 *   picks them up as its own defaults (an explicit flag given directly to
 *   "serve" still always wins over whatever this persisted, the same
 *   "explicit flag beats a persisted default" precedent this project
 *   already follows everywhere else).
 *
 *   --config-file overrides where that config file itself lives --
 *   config_file_path()'s own Debian-style split (routes.h): a systemd
 *   unit or a container entrypoint commonly wants pg_walserver's own
 *   config under /etc/pg_walserver/pg_walserver.ini, independent of
 *   --pgdata (its data root, e.g. /var/lib/pg_walserver). --pgdata is
 *   still created here regardless -- every route's own storage still
 *   lives under it unconditionally.
 *
 *   Deliberately nothing about any one archived cluster -- that's
 *   `pg_walserver register|drop|set-upstream cluster`'s own job
 *   (cli_cluster.h) now, split out of what used to be this same command.
 *   "setup" configures the server; "cluster" configures what it serves.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_SETUP_H
#define WS_CLI_SETUP_H

#include <stdbool.h>

#include "postgres_fe.h"

typedef struct WsSetupOptions
{
	char pgdata[MAXPGPATH];
	char configFile[MAXPGPATH];  /* --config-file: empty means the default,
	                              * see config_file_path()'s own comment */
	bool havePort;
	int port;
	char sslCertFile[MAXPGPATH];
	char sslKeyFile[MAXPGPATH];
	char sslCaFile[MAXPGPATH];
	bool haveAuthTimeout;
	int authTimeout;
} WsSetupOptions;

/*
 * cli_setup_run writes whichever of options's own fields were actually
 * given into the config file's own global section (config_set_global_
 * property(), routes.c) -- see this file's own header comment. Creates
 * --pgdata if it doesn't exist yet. Returns true on success, false with
 * an error already logged otherwise. Never touches any route's own
 * section.
 */
bool cli_setup_run(const WsSetupOptions *options);

#endif /* WS_CLI_SETUP_H */
