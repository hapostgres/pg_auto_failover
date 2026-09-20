/*
 * src/bin/pg_walsender/hba.h
 *   pg_walsender's host-based authentication file, a deliberately small
 *   subset of pg_hba.conf. One rule per line, first match wins, no match
 *   means reject (and so does a missing or unreadable file):
 *
 *     # TYPE  ROUTE            USER                        ADDRESS      METHOD
 *     host    all              pgautofailover_replicator   monitor      trust
 *     host    default/0        pitr_restore                10.1.0.0/16  scram-sha-256
 *
 *   ROUTE is "all" or "<formation>/<group>". USER is "all" or a role name.
 *   ADDRESS is "all", "monitor", an IP address, an IP/prefix, or a
 *   hostname. "monitor" stands for the nodes the monitor lists for the
 *   route: each membership's capture service keeps the nodes file in the
 *   route's storage directory (one hostname per line) current, so a node that
 *   registered with the monitor may connect without any file edit --
 *   while a host that never registers (a PITR restore target, for
 *   instance) needs an explicit line here. METHOD is "trust",
 *   "scram-sha-256" (password checked against the passwd file's stored
 *   verifiers, see auth.h) or "reject".
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
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
 * HBA file. Returns false when the file cannot be read (callers must then
 * reject the connection: fail closed); otherwise sets *method, which is
 * WS_AUTH_REJECT when no rule matches or the matching rule says reject.
 */
bool hba_lookup(const char *hbaPath, const char *routePath,
				const char *routeKey, const char *user, const char *peerIP,
				WsAuthMethod *method);

/* create the default HBA file if there is none; never overwrite one */
bool hba_write_default_if_missing(const char *hbaPath);

/* does hostOrIp name the same address as the numeric peerIP? */
bool hba_host_matches_peer(const char *hostOrIp, const char *peerIP);

#endif /* WS_HBA_H */
