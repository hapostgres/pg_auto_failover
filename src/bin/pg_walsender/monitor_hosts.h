/*
 * src/bin/pg_walsender/monitor_hosts.h
 *   The "monitor" address of the HBA file (hba.h): the nodes the monitor
 *   lists for a route. pg_walsender keeps a local copy of that list in the
 *   route's own directory (archiver-nodes.list: a "# hash <md5>" line, then
 *   one hostname per line) and validates it at connect time instead of
 *   polling:
 *
 *   - a copy older than WS_HOSTS_MAX_AGE_SECONDS is validated first: one
 *     cheap monitor query returns a fingerprint of the current list
 *     (pgautofailover.get_group_hosts_hash(), md5 over the node count and
 *     the sorted names, computed in SQL) and only when it differs from the
 *     copy's own is the list fetched again -- so a node that was dropped
 *     stops being trusted within seconds, without shipping the list on
 *     every connection;
 *   - a peer that is not in an already fresh copy forces one more
 *     validation, at most once per WS_HOSTS_MISS_MIN_AGE_SECONDS, so a node
 *     that registered a moment ago is let in at its first connection while
 *     a flood of unknown peers costs the monitor at most one query per
 *     second.
 *
 *   The monitor is reached with the URI in archiver-monitor.uri (written by
 *   the archiver's reconciler); when it is unreachable the local copy is
 *   used as it is (failing static, not open).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_MONITOR_HOSTS_H
#define WS_MONITOR_HOSTS_H

#include <stdbool.h>

#define WS_HOSTS_MAX_AGE_SECONDS 5
#define WS_HOSTS_MISS_MIN_AGE_SECONDS 1

/*
 * monitor_hosts_contain: is peerIP one of the hosts the monitor lists for
 * routeKey ("<formation>/<group>")? routePath is the route's directory,
 * monitorUriPath the file holding the monitor's connection string (empty:
 * only the local copy is consulted).
 */
bool monitor_hosts_contain(const char *routeKey, const char *routePath,
						   const char *monitorUriPath, const char *peerIP);

#endif /* WS_MONITOR_HOSTS_H */
