/*
 * src/bin/pg_walserver/cli_serve.h
 *   `pg_walserver serve [options]`: run the accept loop (accept_loop.h).
 *   No sub-command is ever implicit -- `pg_walserver` alone (or any
 *   unrecognized/missing sub-command) only prints usage and exits
 *   non-zero -- `pg_walserver serve ...` must always be spelled out.
 *   Applies the config file's own global section (config_load_global(),
 *   routes.c, see cli_setup.h) as its own port/TLS/auth-timeout defaults
 *   whenever the equivalent flag isn't given directly on this command
 *   line.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_SERVE_H
#define WS_CLI_SERVE_H

#include "commandline.h"

extern CommandLine serve_command;

#endif /* WS_CLI_SERVE_H */
