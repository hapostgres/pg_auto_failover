/*
 * src/bin/pg_walserver/cli_ps.c
 *   See cli_ps.h.
 *
 *   Cross-process visibility mechanism: "pg_walserver ps" runs as a brand
 *   new process, entirely separate from any running "pg_walserver serve" --
 *   it cannot read receivewal.c's own in-process receivewalRoutes/receivewalServices
 *   arrays, or accept_loop.c's own bootstrapChildren array, because those
 *   simply do not exist in this process's address space. Two mechanisms
 *   were considered:
 *
 *     - /proc scraping: every embedded receivewal worker child is exec()'d as
 *       "pg_walserver internal service pg-receivewal --route <key> ..."
 *       (receivewal.c), so its own route key IS recoverable from
 *       /proc/<pid>/cmdline by any process willing to walk /proc looking
 *       for it. This would work for the receivewal worker set alone.
 *
 *     - a small state file "serve" itself keeps current (ps_state.h),
 *       written from the exact same in-process data "ps" would otherwise
 *       have to reconstruct.
 *
 *   This file uses the state file (ws_ps_state_read(), ps_state.h), for
 *   two reasons documented in full in ps_state.h's own header comment:
 *   the one-shot bootstrap-backup job is a *plain* fork(), with no
 *   exec() and therefore no distinguishable /proc/<pid>/cmdline at all --
 *   /proc scraping cannot see it, only "serve"'s own in-process
 *   bootstrapChildren[] bookkeeping (accept_loop.c) knows the pid<->route
 *   mapping for it -- and restart counts/precise start times live in
 *   process_supervisor.h's own in-memory ring buffer, which has no /proc
 *   equivalent either. A state file "serve" already has every one of
 *   those answers to write is simpler, and strictly more complete, than
 *   mixing a /proc-based answer for one kind of child with an entirely
 *   different, file-based one for the other two.
 *
 *   Liveness: read_pidfile() (src/bin/common/pidfile.c) already performs a
 *   real kill(pid, 0) check and removes a stale pidfile -- reused as-is,
 *   both to decide whether "serve" itself is running at all, and (this
 *   file's own extra kill(pid, 0) calls) whether a specific receivewal worker/
 *   bootstrap pid the state file remembers is still actually alive: the
 *   state file is refreshed at most once a second (accept_loop.c's own
 *   refresh_ps_state()), so a pid it names could, in the narrow window
 *   since the last refresh, have already exited.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "postgres_fe.h"

#include "cli_ps.h"
#include "file_utils.h"
#include "log.h"
#include "pidfile.h"
#include "ps_state.h"
#include "string_utils.h"


static void
format_uptime(time_t startedAt, char *dest, size_t destSize)
{
	if (startedAt <= 0)
	{
		strlcpy(dest, "-", destSize);
		return;
	}

	long secs = (long) (time(NULL) - startedAt);

	if (secs < 0)
	{
		secs = 0;
	}

	sformat(dest, destSize, "%ldh%02ldm%02lds",
			secs / 3600, (secs % 3600) / 60, secs % 60);
}


bool
cli_ps_run(const char *pgdata)
{
	if (pgdata == NULL || pgdata[0] == '\0')
	{
		log_error("--pgdata is required (or set the PGDATA environment "
				  "variable)");
		return false;
	}

	char pidfilePath[MAXPGPATH] = { 0 };

	sformat(pidfilePath, sizeof(pidfilePath), "%s/pg_walserver.pid", pgdata);

	pid_t servePid = 0;

	if (!read_pidfile(pidfilePath, &servePid))
	{
		printf("pg_walserver is not running (no pidfile at \"%s\")\n", /* IGNORE-BANNED */
			   pidfilePath);
		return true;
	}

	WsPsState state = { 0 };
	bool haveState = ws_ps_state_read(pgdata, &state);

	char serveUptime[32] = { 0 };

	format_uptime(haveState ? state.serveStartedAt : 0, serveUptime,
				  sizeof(serveUptime));

	printf("pg_walserver serve: pid %d, running, uptime %s\n\n", /* IGNORE-BANNED */
		   (int) servePid, serveUptime);

	if (!haveState || (state.receivewalWorkerCount == 0 && state.bootstrapCount == 0))
	{
		printf("No embedded receivewal workers or bootstrap backup jobs.\n"); /* IGNORE-BANNED */
		return true;
	}

	printf("%-11s %-20s %-8s %-9s %-12s %s\n", /* IGNORE-BANNED */
		   "KIND", "CLUSTER", "PID", "STATUS", "UPTIME", "RESTARTS");
	printf("----------------------------------------------------" /* IGNORE-BANNED */
		   "---------------------\n");

	for (int i = 0; i < state.receivewalWorkerCount; i++)
	{
		const WsPsReceivewalEntry *c = &state.receivewalWorkers[i];
		bool running = c->pid > 0 && kill(c->pid, 0) == 0;
		char uptime[32] = { 0 };

		format_uptime(running ? c->startedAt : 0, uptime, sizeof(uptime));

		printf("%-11s %-20s %-8d %-9s %-12s %d\n", /* IGNORE-BANNED */
			   "receivewal", c->routeKey, (int) c->pid,
			   running ? "running" : "stopped", uptime, c->restarts);
	}

	for (int i = 0; i < state.bootstrapCount; i++)
	{
		const WsPsBootstrapEntry *b = &state.bootstraps[i];
		bool running = b->pid > 0 && kill(b->pid, 0) == 0;
		char uptime[32] = { 0 };

		format_uptime(running ? b->startedAt : 0, uptime, sizeof(uptime));

		printf("%-11s %-20s %-8d %-9s %-12s %s\n", /* IGNORE-BANNED */
			   "bootstr", b->routeKey, (int) b->pid,
			   running ? "running" : "done", uptime, "-");
	}

	return true;
}
