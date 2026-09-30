/*
 * src/bin/pg_walserver/cli_scram_secret.h
 *   `pg_walserver scram-secret [--user <name>]`: print one
 *   pg_walserver_passwd line for a user, reading the password from the
 *   PGPASSWORD environment variable, never from the command line.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_SCRAM_SECRET_H
#define WS_CLI_SCRAM_SECRET_H

#include "commandline.h"

extern CommandLine scram_secret_command;

#endif /* WS_CLI_SCRAM_SECRET_H */
