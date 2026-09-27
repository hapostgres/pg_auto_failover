/*
 * src/bin/pg_walserver/accept_loop.h
 *   The bare accept loop: socket()/bind()/listen()/accept(), fork()
 *   per connection with no exec() (matching real Postgres's
 *   BackendStartup()/BackendMain() model for cheap concurrency -- one
 *   process per connection), each forked child running
 *   the full startup/auth/command-loop for exactly one connection.
 *
 *   Hardening, modelled on PostgreSQL's postmaster: every child runs under
 *   an absolute authentication deadline (--auth-timeout, default 30
 *   seconds, armed with alarm() and a SIGALRM handler that _exit()s: the
 *   startup packet, TLS handshake, HBA including DNS and SCRAM all count,
 *   and it is cancelled once authentication succeeded); live children are
 *   tracked in a pid array and reaped in the main loop (no SIGCHLD handler,
 *   no shared counter), capping the connections.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_ACCEPT_LOOP_H
#define WS_ACCEPT_LOOP_H

#include <stdbool.h>

#include "postgres_fe.h"

#include "auth.h"

typedef struct WsServerConfig
{
	int port;
	int authTimeout;              /* absolute authentication deadline, seconds */
	char routesPath[MAXPGPATH];   /* empty: no routing, manual-testing mode */
	WsAuthConfig auth;
} WsServerConfig;

bool ws_accept_loop(const WsServerConfig *config);

#endif /* WS_ACCEPT_LOOP_H */
