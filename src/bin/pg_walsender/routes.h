/*
 * src/bin/pg_walsender/routes.h
 *   The archiver's own "pg_hba.conf" equivalent: a small INI file, one
 *   section per "<formation>/<group>" this archiver serves, mapping the
 *   incoming connection's dbname to a WAL-cache directory, a base-backup
 *   directory, and an optional allowed-hosts list. Written and periodically
 *   refreshed by pg_autoctl's archiver-serve supervisor
 *   (service_archiver_serve.c) from the monitor's archiver_node/basebackup
 *   rows; pg_walsender itself never talks to the monitor (see the
 *   "Routing" section of ~/dev/temp/archiving-disaster-recovery.md's
 *   implementation plan).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_ROUTES_H
#define WS_ROUTES_H

#include <stdbool.h>

#include "postgres_fe.h"

typedef struct WsRoute
{
	char key[NAMEDATALEN + 16];         /* "<formation>/<group>", matches dbname */
	char walcacheDir[MAXPGPATH];
	char basebackupDir[MAXPGPATH];
	char allowedHosts[1024];            /* comma-separated, empty = unrestricted */
	char systemId[32];                  /* decimal uint64, as text; "" = unknown */
	int timeline;                       /* 0 = unknown */
} WsRoute;

/*
 * routes_load parses the routes file at path into a freshly malloc'ed
 * array. Returns true with *routesOut and *countOut set (possibly count
 * == 0 for an empty file) on success, false on a missing/malformed file.
 */
bool routes_load(const char *path, WsRoute **routesOut, int *countOut);
void routes_free(WsRoute *routes);

const WsRoute * routes_find(const WsRoute *routes, int count, const char *key);

/*
 * routes_host_allowed checks peerIP (a numeric address string, as returned
 * by getnameinfo(..., NI_NUMERICHOST)) against route->allowedHosts, which
 * may contain either numeric addresses or hostnames (resolved via DNS at
 * check time). An empty allowedHosts list means "no restriction."
 */
bool routes_host_allowed(const WsRoute *route, const char *peerIP);

#endif /* WS_ROUTES_H */
