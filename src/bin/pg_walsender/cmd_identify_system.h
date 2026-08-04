/*
 * src/bin/pg_walsender/cmd_identify_system.h
 *   IDENTIFY_SYSTEM: reports systemid/timeline/xlogpos/dbname for the
 *   resolved route. See cmd_identify_system.c for what's a placeholder in
 *   this milestone vs. wired to real data.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_IDENTIFY_SYSTEM_H
#define WS_CMD_IDENTIFY_SYSTEM_H

#include "routes.h"

void cmd_identify_system(int sock, const WsRoute *route, const char *dbname);

#endif /* WS_CMD_IDENTIFY_SYSTEM_H */
