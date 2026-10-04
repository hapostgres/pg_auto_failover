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
 *   A sixth, optional field, "clientcert=verify-full", may follow METHOD --
 *   mirroring real PostgreSQL's own pg_hba.conf "clientcert" option (see
 *   src/backend/libpq/hba.c upstream): the TLS peer certificate's CN must
 *   equal USER exactly (no user name mapping, this project has none). With
 *   METHOD "trust" the certificate check IS the whole authentication; with
 *   "scram-sha-256" both the certificate AND the SCRAM exchange must
 *   succeed (two-factor). This project does not implement
 *   "clientcert=verify-ca" -- see auth.c's own header comment for why.
 *   Requires --ssl-ca-file (tls.h) to be loaded at startup; a ruleset with
 *   any such line and no CA loaded fails closed at startup.
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

/* one HBA logical line, already tokenized and validated by hba_parse_file() */
#define WS_HBA_MAX_FIELDS 5

typedef struct HbaRule
{
	char *fields[WS_HBA_MAX_FIELDS];
	WsAuthMethod method;
	bool requireClientCert;    /* "clientcert=verify-full" 6th field */
	int lineNumber;
} HbaRule;

/*
 * WsHbaRuleSet is the whole, already-validated HBA file, kept in memory: the
 * server parses it once at startup and again, tentatively, on SIGHUP (see
 * accept_loop.c's own ws_reload_config()), swapping it in only when the new
 * file parses cleanly -- connections in between keep matching against
 * whichever WsHbaRuleSet is currently installed, never re-reading the file
 * off disk themselves. This mirrors PostgreSQL's own ProcessConfigFile()
 * semantics: a bad reload is refused, not partially applied.
 */
typedef struct WsHbaRuleSet
{
	HbaRule *rules;
	int count;
} WsHbaRuleSet;

bool hba_parse_file(const char *hbaPath, WsHbaRuleSet *ruleSet);

/* releases a WsHbaRuleSet returned by hba_parse_file() */
void hba_ruleset_free(WsHbaRuleSet *ruleSet);

/*
 * hba_match finds the first rule of ruleSet matching (routeKey, user,
 * peerIP), setting *method (WS_AUTH_REJECT when no rule matches or the
 * matching rule says reject). Never fails: ruleSet is already known-valid,
 * having come from a successful hba_parse_file().
 *
 * routeKey is matched against each rule's ROUTE field as a plain, opaque
 * string, exactly the same string routes.c matches against pg_walserver.ini's
 * own section names (the route need not actually exist yet: an unknown
 * route is reported only once the client authenticated, see auth.h) -- HBA
 * admission and pg_walserver.ini's own lookup (including its "*" wildcard, see
 * routes.h) are two entirely independent decisions made from the same
 * key, neither one aware of the other.
 */
void hba_match(const WsHbaRuleSet *ruleSet, const char *routeKey,
			   const char *user, const char *peerIP, bool isTLS,
			   WsAuthMethod *method, bool *requireClientCert);

/* create the default HBA file if there is none; never overwrite one */
bool hba_write_default_if_missing(const char *hbaPath, bool tlsAvailable);

/*
 * Same as hba_write_default_if_missing(), plus an optional localCIDR: when
 * given (non-empty), the one example rule written is active (not commented
 * out), open to that CIDR -- "pg_walserver setup"'s own use, once it has
 * auto-discovered its local network's CIDR with ws_setup_autodetect_cidr()
 * below. NULL/empty gets the exact same commented-out-placeholder behavior
 * as hba_write_default_if_missing() itself.
 */
bool hba_write_setup_default(const char *hbaPath, bool tlsAvailable,
							 const char *localCIDR);

/*
 * ws_setup_autodetect_cidr is a best-effort, non-fatal discovery of this
 * machine's own local-network CIDR -- see hba.c's own comment. Returns
 * false, cidrOut untouched, when nothing usable was found; never fatal.
 */
bool ws_setup_autodetect_cidr(char *cidrOut, size_t cidrOutSize);

bool hba_ruleset_requires_client_cert(const WsHbaRuleSet *ruleSet);

#endif /* WS_HBA_H */
