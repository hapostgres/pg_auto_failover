/*
 * src/bin/pg_walserver/cmd_identify_system.h
 *   IDENTIFY_SYSTEM: reports systemid/timeline/xlogpos/dbname for the
 *   resolved route. See cmd_identify_system.c for what's a placeholder in
 *   the current implementation vs. wired to real data.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_IDENTIFY_SYSTEM_H
#define WS_CMD_IDENTIFY_SYSTEM_H

#include "routes.h"

void cmd_identify_system(int sock, const WsRoute *route, const char *dbname);

#endif /* WS_CMD_IDENTIFY_SYSTEM_H */
