/*
 * src/bin/pg_walserver/cli_common.h
 *   Tiny CLI plumbing shared by nearly every pg_walserver sub-command's own
 *   getopt callback -- currently just ws_prefill_pgdata_from_env(), see
 *   cli_common.c's own header comment.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_COMMON_H
#define WS_CLI_COMMON_H

void ws_prefill_pgdata_from_env(char *pgdata);

#endif /* WS_CLI_COMMON_H */
