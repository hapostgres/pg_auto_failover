/*
 * src/bin/pg_walserver/receivewal.h
 *   The embedded, supervised WAL receivewal worker for every route with
 *   "receivewal = pull" configured (routes.h) -- see receivewal.c's own header
 *   comment for the full design (fork()+execv() shape, the shared
 *   process_supervisor.h it's built on, restart/backoff policy, and the
 *   single-wildcard-reaper contract ws_receivewal_tick() has with its
 *   caller) and README.md's "The embedded receivewal worker" section for the
 *   design this implements.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_RECEIVEWAL_H
#define WS_RECEIVEWAL_H

#include <stdbool.h>
#include <sys/types.h>

#include "postgres_fe.h"

#include "routes.h"

bool ws_receivewal_start_all(const WsRoute *routes, int routeCount);

void ws_receivewal_tick(bool (*otherChildExited)(void *ctx, pid_t pid,
												 int status),
						void *otherCtx);

void ws_receivewal_reload(const WsRoute *newRoutes, int newRouteCount);

/*
 * WsReceivewalStatus is one supervised "receivewal = pull" receivewal worker's current
 * status, as seen from inside "serve" itself -- see ws_receivewal_get_status()
 * below, and ps_state.h for why a *different* process (pg_walserver ps/
 * status) cannot just read receivewalServices/receivewalRoutes directly and
 * instead goes through a state file "serve" writes from this same data.
 */
typedef struct WsReceivewalStatus
{
	char routeKey[NAMEDATALEN + 16];
	char path[MAXPGPATH];
	char upstream[MAXCONNINFO];
	pid_t pid;          /* <= 0: not currently running */
	time_t startedAt;   /* this incarnation's own start time */
	int restarts;        /* how many times it has been restarted */
} WsReceivewalStatus;

int ws_receivewal_get_status(WsReceivewalStatus *out, int maxOut);

void ws_receivewal_stop_all(void);

#endif /* WS_RECEIVEWAL_H */
