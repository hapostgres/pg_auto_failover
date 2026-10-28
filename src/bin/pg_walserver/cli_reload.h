/*
 * src/bin/pg_walserver/cli_reload.h
 *   `pg_walserver reload --pgdata <path>`: send SIGHUP to a running
 *   "serve" instance (its pid read from <pgdata>/pg_walserver.pid) to
 *   re-read pg_walserver.ini/pg_walserver_hba.conf and reconcile the
 *   embedded receivewal worker set -- see accept_loop.c's own
 *   ws_reload_config()/ws_receivewal_reload().
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_RELOAD_H
#define WS_CLI_RELOAD_H

#include "commandline.h"

extern CommandLine reload_command;

#endif /* WS_CLI_RELOAD_H */
