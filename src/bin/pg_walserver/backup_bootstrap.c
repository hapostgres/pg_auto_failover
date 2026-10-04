/*
 * src/bin/pg_walserver/backup_bootstrap.c
 *   See backup_bootstrap.h.
 *
 *   Design decision: a plain fork(), never execv(). Every other long-lived
 *   child this project forks (the embedded receivewal worker, receivewal.c) execv()
 *   s itself back into a hidden "internal service ..." sub-command so a
 *   restarted child always picks up whatever binary is currently on disk --
 *   the live-upgrade-safety property that matters for a process that is
 *   supervised and may be restarted many times over the server's whole
 *   lifetime. Taking one route's bootstrap backup is the opposite shape: a
 *   single, one-time, transient job that runs once and exits, reusing the
 *   already-public "pg_walserver basebackup" logic (cli_basebackup.c)
 *   in-process -- there is no new "internal service basebackup" hidden
 *   entry point here, deliberately: inventing one would just be indirection
 *   around a function call this same binary can already make directly,
 *   right after fork(), with no exec() needed at all.
 *
 *   Bounded retries, not indefinite ones: this is not a supervised
 *   ProcessService (process_supervisor.h's own Erlang-inspired MaxR/MaxT
 *   ring buffer is built for a long-lived service that gets restarted many
 *   times over a process's whole lifetime, tracking restarts against a
 *   sliding time window -- overkill for a single child that runs once and
 *   exits). A simple, small, fixed retry count within that one child's own
 *   lifetime is the simplest correct fit: WS_BOOTSTRAP_BACKUP_MAX_ATTEMPTS
 *   attempts, a short fixed delay between them, then give up, log a clear
 *   error, and exit nonzero -- accept_loop.c's own ws_bootstrap_missing_
 *   backups() is called again at the *next* startup or reload, which will
 *   retry a still-missing backup then, so nothing is lost by not retrying
 *   forever inside a single child.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <unistd.h>

#include "postgres_fe.h"

#include "backup_bootstrap.h"
#include "cli_basebackup.h"
#include "cli_upstream.h"
#include "file_utils.h"
#include "log.h"
#include "string_utils.h"
#include "wal_dir_scan.h"

/*
 * How long to wait for a route's own real, already-started receivewal worker to show
 * on-disk evidence of streaming before giving up on it -- the same bounded
 * timeout the removed "setup --with-basebackup" priming code used to poll
 * with (wal_dir_has_any_segment()), reused here against a real, supervised
 * receivewal worker instead of a throwaway primer.
 */
#define WS_BOOTSTRAP_STREAM_WAIT_TIMEOUT_MS 30000
#define WS_BOOTSTRAP_STREAM_WAIT_POLL_MS 100

/* small, fixed retry bound for the backup attempt itself -- see this file's
 * own header comment for why this isn't the full MaxR/MaxT machinery */
#define WS_BOOTSTRAP_BACKUP_MAX_ATTEMPTS 3
#define WS_BOOTSTRAP_BACKUP_RETRY_DELAY_S 5


/*
 * bootstrap_child_main runs entirely inside the forked child: wait for real
 * streaming evidence (receivewal = pull routes only), then attempt the backup
 * itself, bounded. Never returns -- always _exit()s.
 */
static void
bootstrap_child_main(const WsRoute *route)
{
	/*
	 * A plain fork(), no execv(): without this, the child's own /proc/pid/
	 * cmdline (and therefore anything matching on it, e.g. a test suite's
	 * own "pgrep -f '^pg_walserver ... serve$'") stays byte-for-byte
	 * identical to the parent's, which "serve" is not -- exactly the same
	 * reason receivewal.c's own receivewal worker children (via a fresh execv()) and
	 * cli_internal.c's own entry point end up with a distinct process
	 * title. This one has no execv() to do that for free, so it sets its
	 * own title explicitly, the instant it exists.
	 */
	char title[256];

	sformat(title, sizeof(title), "pg_walserver: bootstrap-backup %s",
			route->key);
	set_ps_title(title);

	if (route->receivewalPull)
	{
		bool streaming = false;
		int elapsedMs = 0;

		log_info("Route \"%s\": waiting for its embedded receivewal worker to "
				 "start streaming before taking the bootstrap base backup",
				 route->key);

		while (elapsedMs < WS_BOOTSTRAP_STREAM_WAIT_TIMEOUT_MS)
		{
			if (wal_dir_has_any_segment(route))
			{
				streaming = true;
				break;
			}

			pg_usleep(WS_BOOTSTRAP_STREAM_WAIT_POLL_MS * 1000);
			elapsedMs += WS_BOOTSTRAP_STREAM_WAIT_POLL_MS;
		}

		if (!streaming)
		{
			log_error("Route \"%s\": timed out after %d ms waiting for its "
					  "embedded receivewal worker to start streaming any WAL at "
					  "all -- giving up on the automatic bootstrap base "
					  "backup; run \"pg_walserver basebackup\" by hand once "
					  "the receivewal worker is healthy", route->key,
					  WS_BOOTSTRAP_STREAM_WAIT_TIMEOUT_MS);
			_exit(1);
		}
	}

	WsUpstreamTarget target = { 0 };

	if (!cli_resolve_upstream(NULL, NULL, NULL, route->path, route->upstream,
							  NULL, NULL, NULL, &target))
	{
		/* errors have already been logged */
		_exit(1);
	}

	bool ok = false;

	for (int attempt = 1;
		 attempt <= WS_BOOTSTRAP_BACKUP_MAX_ATTEMPTS && !ok;
		 attempt++)
	{
		log_info("Route \"%s\": taking its automatic bootstrap base backup "
				 "(attempt %d/%d)", route->key, attempt,
				 WS_BOOTSTRAP_BACKUP_MAX_ATTEMPTS);

		ok = cli_basebackup_run(&target, NULL, 0);

		if (!ok && attempt < WS_BOOTSTRAP_BACKUP_MAX_ATTEMPTS)
		{
			sleep(WS_BOOTSTRAP_BACKUP_RETRY_DELAY_S);
		}
	}

	if (!ok)
	{
		log_error("Route \"%s\": giving up on the automatic bootstrap base "
				  "backup after %d attempt%s -- the route keeps serving "
				  "whatever it already has; run \"pg_walserver basebackup\" "
				  "by hand (or from your own cron job) to give it one",
				  route->key, WS_BOOTSTRAP_BACKUP_MAX_ATTEMPTS,
				  WS_BOOTSTRAP_BACKUP_MAX_ATTEMPTS == 1 ? "" : "s");
		_exit(1);
	}

	log_info("Route \"%s\": automatic bootstrap base backup complete",
			 route->key);
	_exit(0);
}


/*
 * ws_backup_bootstrap_start forks a plain child (no execv(): this is a
 * one-time transient operation, not a long-lived service needing receivewal.
 * c's own fork()+execv()-for-live-upgrade treatment) that takes route's
 * first base backup and exits -- never blocks the caller beyond the
 * fork() call itself. Returns true with *pidOut set once the child has
 * been forked (the caller is responsible for eventually reaping it, the
 * same way it already reaps every other child it forks); false, with an
 * error already logged, only if fork() itself failed.
 *
 * The child, in order:
 *
 *   - for a "receivewal = pull" route, waits (bounded, see backup_bootstrap.c's
 *     own WS_BOOTSTRAP_STREAM_WAIT_* constants) for wal_dir_has_any_segment()
 *     to become true against route's own real, already-started, supervised
 *     receivewal worker (receivewal.c) -- never a throwaway primer, unlike the removed
 *     "setup --with-basebackup" design this replaces: by the time this
 *     function is ever called, "serve" has already started (or already
 *     reconciled, on reload) route's own real receivewal worker, so there is always
 *     a genuine one to wait on directly;
 *   - takes the backup itself (cli_basebackup_run(), cli_basebackup.c),
 *     retried up to WS_BOOTSTRAP_BACKUP_MAX_ATTEMPTS times with a short
 *     delay between attempts -- bounded, never an infinite retry loop;
 *   - logs a clear error and exits nonzero on final failure. The route
 *     keeps serving whatever it already has either way; an operator's own
 *     "pg_walserver basebackup" (or their own cron job around it) is what
 *     eventually gets such a route a backup -- this project provides the
 *     facility, not the scheduling policy, the same philosophy a future
 *     "archive-cleanup"-style command is expected to follow too.
 */
bool
ws_backup_bootstrap_start(const WsRoute *route, pid_t *pidOut)
{
	fflush(stdout);
	fflush(stderr);

	pid_t pid = fork();

	if (pid == -1)
	{
		log_error("Route \"%s\": failed to fork the automatic bootstrap "
				  "base backup job: %m", route->key);
		return false;
	}

	if (pid == 0)
	{
		bootstrap_child_main(route);

		/* unreachable: bootstrap_child_main() always _exit()s */
		_exit(1);
	}

	*pidOut = pid;

	return true;
}
