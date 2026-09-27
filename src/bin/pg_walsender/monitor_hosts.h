/*
 * src/bin/pg_walsender/monitor_hosts.h
 *   The "monitor" address of the HBA file (hba.h): the nodes the monitor
 *   lists for a route. The list lives in the route's own directory as
 *   archiver-nodes.list ("# hash <md5>" first line, then one hostname per
 *   line), and it is built the way PostgreSQL treats its shared,
 *   authentication relevant state:
 *
 *   - ONE WRITER, READERS READ-ONLY. The connection children (one process
 *     per connection, like a PostgreSQL backend) never talk to the monitor
 *     and never write a file. Just as the postmaster loads pg_hba.conf once
 *     and every backend inherits it read-only by fork(), and as the
 *     historical flat files (global/pg_auth, pg_database) were rewritten by
 *     a single writer with write-temp-then-rename so that a reader sees
 *     either the old or the new file and never a torn one, here a single
 *     supervised REFRESHER process (refresher.c), forked by the accept loop
 *     parent, is the only process that queries the monitor (libpq) and the
 *     only writer of the list (temp file + fsync + rename).
 *
 *   - VALIDATION AT CONNECT TIME, WITHOUT POLLING. A child needing a
 *     verdict (list missing, older than WS_HOSTS_MAX_AGE_SECONDS, or the
 *     peer not found in a list older than WS_HOSTS_MISS_MIN_AGE_SECONDS)
 *     sends a small datagram carrying the route key to the refresher, over
 *     an AF_UNIX SOCK_DGRAM socket (<pgdata>/archiver-refresh.sock, mode
 *     0600, created by the parent before it forks anything), then waits
 *     (polling the file's mtime every 20ms, at most WS_HOSTS_WAIT_MAX_MS
 *     and never past the authentication deadline) for the file to be
 *     renewed, and finally reads it read-only. The refresher COALESCES
 *     requests: at most one cheap monitor query per route per second (the
 *     fingerprint pgautofailover.get_group_hosts_hash(), md5 over the node
 *     count and the sorted names); an unchanged fingerprint only utimes()
 *     the file, a changed one fetches and atomically rewrites it. So a node
 *     that was dropped stops being trusted within seconds, a node that just
 *     registered is let in at its first connection, and a flood of unknown
 *     peers costs the monitor at most one query per second.
 *
 *   - FAIL CLOSED. The refresher negative-caches monitor failures
 *     (WS_HOSTS_NEGATIVE_SECONDS: no connection attempt per request, and a
 *     "<list>.err" marker tells the children not to wait for it). While the
 *     monitor is unreachable the last list is used as it is (failing
 *     static) only until it is WS_HOSTS_HARD_MAX_AGE_SECONDS old; older
 *     than that the "monitor" address matches nothing (logged once).
 *
 *   Without a monitor URI file (archiver-monitor.uri) there is no refresher
 *   and the list, if any, is used as it is.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_MONITOR_HOSTS_H
#define WS_MONITOR_HOSTS_H

#include <stdbool.h>
#include <stddef.h>

#define WS_HOSTS_MAX_AGE_SECONDS 5
#define WS_HOSTS_MISS_MIN_AGE_SECONDS 1
#define WS_HOSTS_HARD_MAX_AGE_SECONDS 3600
#define WS_HOSTS_WAIT_MAX_MS 3000
#define WS_HOSTS_NEGATIVE_SECONDS 5

#define WS_HOSTS_HASH_LINE_PREFIX "# hash "
#define WS_HOSTS_HASH_LEN 32
#define WS_HOSTS_ERR_SUFFIX ".err"

/* "<routePath>/archiver-nodes.list" */
void monitor_hosts_list_path(const char *routePath, char *dest,
							 size_t destSize);

/*
 * monitor_hosts_contain: is peerIP one of the hosts the monitor lists for
 * routeKey ("<formation>/<group>")? routePath is the route's directory,
 * monitorUriPath the file holding the monitor's connection string (empty or
 * missing: only the local list is consulted, no refresh is requested),
 * refreshSockPath the refresher's datagram socket. Read-only: never
 * touches the monitor, never writes a file.
 */
bool monitor_hosts_contain(const char *routeKey, const char *routePath,
						   const char *monitorUriPath,
						   const char *refreshSockPath, const char *peerIP);

#endif /* WS_MONITOR_HOSTS_H */
