/*
 * src/bin/pg_walserver/ws_util.c
 *   See ws_util.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include "postgres_fe.h"

#include "system_utils.h"
#include "ws_util.h"


static int64_t authDeadlineMs = 0;   /* 0: none armed */

/*
 * ws_auth_deadline_set arms the current connection's absolute
 * authentication deadline, seconds from now (CLOCK_MONOTONIC-based, see
 * monotonic_ms(), common/system_utils.c, unaffected by wall-clock
 * adjustments), read back by accept_loop.c's own SIGALRM arming -- see
 * README.md's "Process model" section for the full startup/TLS
 * handshake/HBA/SCRAM window it covers.
 */
void
ws_auth_deadline_set(int seconds)
{
	authDeadlineMs = monotonic_ms() + (int64_t) seconds * 1000;
}


/*
 * ws_auth_deadline_clear disarms the connection's authentication deadline
 * (the countdown started by ws_auth_deadline_set()), called once
 * authentication has succeeded.
 */
void
ws_auth_deadline_clear(void)
{
	authDeadlineMs = 0;
}
