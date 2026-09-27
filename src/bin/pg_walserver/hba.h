/*
 * src/bin/pg_walserver/hba.h
 *   pg_walserver's host-based authentication file, a deliberately small
 *   subset of pg_hba.conf. One rule per line, first match wins, no match
 *   means reject (and so does a missing, oversize or unreadable file, and a
 *   file with a malformed line: it is refused as a whole, never partly
 *   applied):
 *
 *     # TYPE     ROUTE       USER                       ADDRESS       METHOD
 *     hostssl    all         pgautofailover_replicator  10.1.0.0/16   scram-sha-256
 *     hostssl    default/0   pitr_restore               192.0.2.0/24  scram-sha-256
 *     hostnossl  all         all                        all           reject
 *
 *   TYPE is "host" (any connection), "hostssl" (TLS only) or "hostnossl".
 *   ROUTE is "all", or a route key exactly as it appears in pg_walserver.ini
 *   (see routes.h) -- an opaque, operator-chosen string pg_walserver never
 *   parses. "default/0" above is pg_auto_failover's own convention
 *   ("<formation>/<group>"), used because it reads well and is already
 *   guaranteed unique across a whole pg_auto_failover deployment -- it is
 *   NOT a path, and the "/" carries no filesystem meaning here at all; a
 *   route key of "archive1", "customer-42" or any other string an operator
 *   finds convenient works exactly the same way. USER is "all" or a role
 *   name. ADDRESS is "all", "samehost", "samenet" (as in PostgreSQL), an IP
 *   address, an IP/prefix, a hostname (resolved forward, every A/AAAA
 *   answer compared), or ".domain.suffix" (every reverse-DNS name of the
 *   client is tried, each confirmed by a forward lookup -- PostgreSQL only
 *   looks at the first answer, which is wrong for hosts and Docker
 *   networks that have several names). There is no monitor-backed
 *   automatic node admission in this PR (a later "archiving" PR
 *   reintroduces a "monitor" ADDRESS keyword once a monitor extension
 *   actually exists for it to query): every host that may connect needs an
 *   explicit line here. METHOD is "scram-sha-256" (password checked
 *   against the stored verifiers in the passwd file, see auth.h), "trust"
 *   or "reject".
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
 * routeKey is matched against each rule's ROUTE field as a plain, opaque
 * string, exactly the same string routes.c matches against pg_walserver.ini's
 * own section names (the route need not actually exist yet: an unknown
 * route is reported only once the client authenticated, see auth.h) -- HBA
 * admission and pg_walserver.ini's own lookup (including its "*" wildcard, see
 * routes.h) are two entirely independent decisions made from the same
 * key, neither one aware of the other.
 */
bool hba_lookup(const char *hbaPath, const char *routeKey, const char *user,
				const char *peerIP, bool isTLS, WsAuthMethod *method);

/* create the default HBA file if there is none; never overwrite one */
bool hba_write_default_if_missing(const char *hbaPath, bool tlsAvailable);

#endif /* WS_HBA_H */
