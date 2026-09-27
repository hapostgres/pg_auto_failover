/*
 * src/bin/pg_walsender/hba.h
 *   pg_walsender's host-based authentication file, a deliberately small
 *   subset of pg_hba.conf. One rule per line, first match wins, no match
 *   means reject (and so does a missing, oversize or unreadable file, and a
 *   file with a malformed line: it is refused as a whole, never partly
 *   applied):
 *
 *     # TYPE     ROUTE       USER                       ADDRESS       METHOD
 *     hostssl    all         pgautofailover_replicator  monitor       scram-sha-256
 *     hostssl    default/0   pitr_restore               10.1.0.0/16   scram-sha-256
 *     hostnossl  all         all                        all           reject
 *
 *   TYPE is "host" (any connection), "hostssl" (TLS only) or "hostnossl".
 *   ROUTE is "all" or "<formation>/<group>". USER is "all" or a role name.
 *   ADDRESS is "all", "samehost", "samenet" (as in PostgreSQL), "monitor", an
 *   IP address, an IP/prefix, a hostname
 *   (resolved forward, every A/AAAA answer compared), or ".domain.suffix"
 *   (every reverse-DNS name of the client is tried, each confirmed by a
 *   forward lookup -- PostgreSQL only looks at the first answer, which is
 *   wrong for hosts and Docker networks that have several names).
 *   "monitor" stands for the nodes the monitor lists for the route, see
 *   monitor_hosts.h: a node that registered may connect without any file
 *   edit, while a host that never registers (a PITR restore target, for
 *   instance) needs an explicit line here. METHOD is "scram-sha-256"
 *   (password checked against the stored verifiers in the passwd file, see
 *   auth.h), "trust" or "reject".
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_HBA_H
#define WS_HBA_H

#include <stdbool.h>

typedef enum WsAuthMethod
{
	WS_AUTH_REJECT = 0,
	WS_AUTH_TRUST,
	WS_AUTH_SCRAM
} WsAuthMethod;

/*
 * hba_lookup finds the first rule matching (routeKey, user, peerIP) in the
 * HBA file. Returns false when the file cannot be read, is larger than
 * 1 MiB, or has ANY malformed line (reported with its line number): callers
 * must then reject the connection, failing closed like PostgreSQL, which
 * refuses to load a bad pg_hba.conf. Otherwise sets *method, which is
 * WS_AUTH_REJECT when no rule matches or the matching rule says reject.
 *
 * routePath is NULL when the client asked for a route that does not exist:
 * the "monitor" address then matches nothing (and no monitor list is read),
 * and the unknown route is reported only once the client authenticated,
 * see auth.h. refreshSockPath is where monitor_hosts.h sends its
 * "please revalidate" datagrams.
 */
bool hba_lookup(const char *hbaPath, const char *routePath,
				const char *monitorUriPath, const char *refreshSockPath,
				const char *routeKey, const char *user, const char *peerIP,
				bool isTLS, WsAuthMethod *method);

/* create the default HBA file if there is none; never overwrite one */
bool hba_write_default_if_missing(const char *hbaPath, bool tlsAvailable);

#endif /* WS_HBA_H */
