/*
 * src/bin/pg_walserver/ws_util.h
 *   The connection's absolute authentication deadline -- genuinely
 *   pg_walserver-specific, tied to accept_loop.c's SIGALRM-based connection
 *   auth window design. Every other helper that used to live here (capped
 *   file reads, a served-file open, log sanitizing, a monotonic clock) is
 *   generic and has moved to src/bin/common/ (file_utils.h,
 *   string_utils.h, system_utils.h respectively).
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_UTIL_H
#define WS_UTIL_H

/*
 * The absolute authentication deadline of the current connection, see
 * accept_loop.c.
 */
void ws_auth_deadline_set(int seconds);
void ws_auth_deadline_clear(void);

#endif /* WS_UTIL_H */
