/*
 * src/bin/pg_walserver/cli_setup.h
 *   `pg_walserver setup --pgdata <path> [--config <path>]
 *   [--port <port>] [--ssl-cert-file <path>] [--ssl-key-file <path>]
 *   [--ssl-ca-file <path>] [--auth-timeout <seconds>]`: configures
 *   pg_walserver *itself* -- writes whichever of these were given as
 *   plain "key = value" lines at the top of its own config file
 *   (config_set_global_property(), clusters.c), so "pg_walserver serve
 *   --pgdata <path>" alone, with none of these flags repeated, already
 *   picks them up as its own defaults (an explicit flag given directly to
 *   "serve" still always wins over whatever this persisted, the same
 *   "explicit flag beats a persisted default" precedent this project
 *   already follows everywhere else).
 *
 *   --config overrides where that config file itself lives --
 *   config_file_path()'s own Debian-style split (clusters.h): a systemd
 *   unit or a container entrypoint commonly wants pg_walserver's own
 *   config under /etc/pg_walserver/pg_walserver.ini, independent of
 *   --pgdata (its data root, e.g. /var/lib/pg_walserver). --pgdata is
 *   still created here regardless -- every cluster's own storage still
 *   lives under it unconditionally.
 *
 *   Also prepares everything else a first "pg_walserver serve" needs
 *   that doesn't require an operator-chosen secret: a self-signed TLS
 *   certificate (create_certificate(), cli_create_cert.c, the exact same
 *   facility "cluster register" itself already uses), and an HBA file
 *   with one real, active rule rather than the commented-out placeholder
 *   "serve" itself falls back to -- open to this machine's own local
 *   network, auto-discovered with ws_setup_autodetect_cidr() (hba.h),
 *   the same idea as pg_autoctl's own LAN-CIDR HBA auto-admission
 *   (pghba_enable_lan_cidr(), src/bin/pg_autoctl/pghba.c) adapted to
 *   "setup" having no remote/upstream target to discover it *from* (see
 *   ws_setup_autodetect_cidr()'s own comment, hba.c). Neither step
 *   overwrites a file that already exists; discovery failing (no usable
 *   local interface) only skips the HBA step, never fails setup as a
 *   whole. Deliberately NOT written here: the password file
 *   (pg_walserver_passwd, "scram-secret" writes to it) and the HBA
 *   rule's own role name/password both need an operator-chosen secret
 *   setup cannot fabricate safely.
 *
 *   Deliberately nothing about any one archived cluster -- that's
 *   `pg_walserver cluster register|drop|list|set-upstream`'s own job
 *   (cli_cluster.h) now, split out of what used to be this same command.
 *   "setup" configures the server; "cluster" configures what it serves.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_SETUP_H
#define WS_CLI_SETUP_H

#include <stdbool.h>

#include "commandline.h"

#include "postgres_fe.h"

typedef struct WsSetupOptions
{
	char pgdata[MAXPGPATH];
	char configFile[MAXPGPATH];  /* --config: empty means the default, see
	                              * config_file_path()'s own comment */
	bool havePort;
	int port;
	char sslCertFile[MAXPGPATH];
	char sslKeyFile[MAXPGPATH];
	char sslCaFile[MAXPGPATH];
	bool haveAuthTimeout;
	int authTimeout;
	bool noHba;    /* --no-hba: skip the HBA auto-provisioning step below */
	bool noCert;   /* --no-cert: skip the TLS certificate auto-creation */
} WsSetupOptions;

bool cli_setup_run(const WsSetupOptions *options);

extern CommandLine setup_command;

#endif /* WS_CLI_SETUP_H */
