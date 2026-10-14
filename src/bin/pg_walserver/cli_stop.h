/*
 * src/bin/pg_walserver/cli_stop.h
 *   `pg_walserver stop --pgdata <path>`: send SIGTERM to a running
 *   "serve" instance (its pid read from <pgdata>/pg_walserver.pid),
 *   the exact same shape as "pg_ctl stop".
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_STOP_H
#define WS_CLI_STOP_H

#include "commandline.h"

extern CommandLine stop_command;

#endif /* WS_CLI_STOP_H */
