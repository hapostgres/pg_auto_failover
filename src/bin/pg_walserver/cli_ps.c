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

#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "postgres_fe.h"

#include "commandline.h"

#include "cli_common.h"
#include "cli_ps.h"
#include "cli_root.h"
#include "file_utils.h"
#include "log.h"
#include "pidfile.h"
#include "ps_state.h"
#include "string_utils.h"


/*
 * format_uptime renders the time elapsed since startedAt as "<h>h<mm>m<ss>s"
 * (e.g. "0h04m31s"), or "-" when startedAt is unset (<= 0).
 */
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


/*
 * cli_ps_run -- see cli_ps.h's own comment.
 */
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
		fformat(stdout, "pg_walserver is not running (no pidfile at \"%s\")\n",
				pidfilePath);
		return true;
	}

	WsPsState state = { 0 };
	bool haveState = ws_ps_state_read(pgdata, &state);

	char serveUptime[32] = { 0 };

	format_uptime(haveState ? state.serveStartedAt : 0, serveUptime,
				  sizeof(serveUptime));

	int childCount = haveState ? (state.receivewalWorkerCount + state.bootstrapCount) : 0;

	fformat(stdout, "pg_walserver(%d) running, uptime %s\n",
			(int) servePid, serveUptime);

	if (childCount == 0)
	{
		return true;
	}

	int printed = 0;

	for (int i = 0; i < state.receivewalWorkerCount; i++)
	{
		const WsPsReceivewalEntry *c = &state.receivewalWorkers[i];
		bool running = c->pid > 0 && kill(c->pid, 0) == 0;
		char uptime[32] = { 0 };
		bool isLast = (++printed == childCount);

		format_uptime(running ? c->startedAt : 0, uptime, sizeof(uptime));

		char lsnStr[64] = { 0 };

		if (c->lsn[0] != '\0')
		{
			long age = (long) (time(NULL) - c->lsnObservedAt);

			if (age < 0)
			{
				age = 0;
			}

			sformat(lsnStr, sizeof(lsnStr), ", lsn %s (timeline %u, %lds ago)",
					c->lsn, c->lsnTimeline, age);
		}

		fformat(stdout, "%s receivewal(%d) %s, %s, uptime %s, restarts %d%s\n",
				isLast ? "`--" : "|--",
				(int) c->pid, c->routeKey,
				running ? "running" : "stopped", uptime, c->restarts, lsnStr);
	}

	for (int i = 0; i < state.bootstrapCount; i++)
	{
		const WsPsBootstrapEntry *b = &state.bootstraps[i];
		bool running = b->pid > 0 && kill(b->pid, 0) == 0;
		char uptime[32] = { 0 };
		bool isLast = (++printed == childCount);

		format_uptime(running ? b->startedAt : 0, uptime, sizeof(uptime));

		fformat(stdout, "%s bootstrap(%d) %s, %s, uptime %s\n",
				isLast ? "`--" : "|--",
				(int) b->pid, b->routeKey,
				running ? "running" : "done", uptime);
	}

	return true;
}


/* -----------------------------------------------------------------------
 * pg_walserver ps --pgdata <path>
 * ----------------------------------------------------------------------- */

static char psPgdata[MAXPGPATH] = { 0 };

static struct option psLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ NULL, 0, NULL, 0 }
};

static int cli_ps_getopt(int argc, char **argv);
static void cli_ps_command_run(int argc, char **argv);

CommandLine ps_command =
	make_command("ps",
				 "Show pg_walserver serve's own process-level status "
				 "(pid, receivewal workers, bootstrap jobs)",
				 "--pgdata <path>",
				 "  --pgdata    this instance's own top-level storage root "
				 "(defaults to\n"
				 "              PGDATA)\n",
				 cli_ps_getopt, cli_ps_command_run);


/*
 * cli_ps_getopt parses "pg_walserver ps"'s own flags into the
 * file-scope statics above.
 */
static int
cli_ps_getopt(int argc, char **argv)
{
	optind = 0;
	ws_prefill_pgdata_from_env(psPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:", psLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(psPgdata, optarg, sizeof(psPgdata));
				break;
			}

			default:
			{
				commandline_print_usage(&ws_root, stderr);
				exit(1);
			}
		}
	}

	return optind;
}


/*
 * cli_ps_command_run runs "pg_walserver ps" against the options cli_ps_getopt
 * parsed above, then exit()s with its own result.
 */
static void
cli_ps_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(cli_ps_run(psPgdata) ? 0 : 1);
}
