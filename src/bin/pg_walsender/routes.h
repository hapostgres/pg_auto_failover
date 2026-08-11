/*
 * src/bin/pg_walsender/routes.h
 *   The archiver's own "pg_hba.conf" equivalent: a small INI file, one
 *   section per "<formation>/<group>" this archiver serves, mapping the
 *   incoming connection's dbname to that membership's own local storage
 *   root and an optional allowed-hosts list. Written by pg_autoctl's
 *   archiver reconciler (service_archiver_reconciler.c) whenever a
 *   membership is added or removed -- the only two moments this mapping
 *   actually changes.
 *
 *   Deliberately just a path: which base backup is current, this group's
 *   system identifier, and the current WAL position are NOT carried here.
 *   Each command that needs one of those reads it fresh, straight from a
 *   small purpose-built file under that same path, at connection time --
 *   see cmd_base_backup.c's own basebackups/.latest and cmd_identify_
 *   system.c's own archiver-systemid for the two current examples. pg_
 *   walsender itself never talks to the monitor (see the "Routing" section
 *   of ~/dev/temp/archiving-disaster-recovery.md's implementation plan,
 *   and archiving-details.rst's "Keeping local files current" section
 *   for the full rationale behind this split).
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
	char path[MAXPGPATH];               /* this membership's own local storage
	                                     * root -- WAL cache, basebackups/,
	                                     * and archiver-systemid all live
	                                     * directly under it */
	char allowedHosts[1024];            /* comma-separated, empty = unrestricted */
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
