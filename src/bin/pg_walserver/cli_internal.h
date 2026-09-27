/*
 * src/bin/pg_walserver/cli_internal.h
 *   `pg_walserver internal service pg-receivewal ...`: the subprocess
 *   entry point capture.c's own supervised capturer children fork()+
 *   exec() into -- see cli_internal.c's own header comment and capture.c's
 *   for why fork()+exec() of this hidden sub-command, not a bare fork().
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_INTERNAL_H
#define WS_CLI_INTERNAL_H

#include "commandline.h"

extern CommandLine internal_commands;

#endif /* WS_CLI_INTERNAL_H */
