/*
 * src/bin/pg_walserver/cli_status.c
 *   See cli_status.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "postgres_fe.h"

#include "cli_status.h"
#include "log.h"
#include "pidfile.h"
#include "ps_state.h"
#include "routes.h"
#include "string_utils.h"


/*
 * format_uptime renders the time elapsed since startedAt as "<h>h<mm>m<ss>s"
 * (e.g. "0h04m31s"), or "-" when startedAt is unset (<= 0, "serve" isn't
 * running).
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
 * cli_status_run -- see cli_status.h's own comment.
 */
bool
cli_status_run(const char *pgdata, const char *configFile)
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
	bool running = read_pidfile(pidfilePath, &servePid);

	if (!running)
	{
		fformat(stdout, "pg_walserver: not running (--pgdata \"%s\")\n", pgdata);
		return true;
	}

	/*
	 * How many routes are configured with "receivewal = pull", the
	 * denominator for the "N/M running" line below -- a process-workforce
	 * fact (how many supervised children are *supposed* to be running),
	 * not the data-layer facts (backup/WAL presence, which cluster is
	 * which) that :ref:`pg_walserver_ls`/:ref:`pg_walserver_list` already
	 * own; status is deliberately only ever about this process and its
	 * own child processes, never the archive data those processes
	 * maintain.
	 */
	char routesPath[MAXPGPATH] = { 0 };

	config_file_path(pgdata, configFile, routesPath, sizeof(routesPath));

	WsRoute *routes = NULL;
	int routeCount = 0;
	int receivewalPullCount = 0;

	if (routes_load(routesPath, &routes, &routeCount))
	{
		for (int i = 0; i < routeCount; i++)
		{
			if (routes[i].receivewalPull)
			{
				receivewalPullCount++;
			}
		}
	}

	WsPsState state = { 0 };
	bool haveState = ws_ps_state_read(pgdata, &state);

	int receivewalWorkersRunning = 0;

	if (haveState)
	{
		for (int i = 0; i < state.receivewalWorkerCount; i++)
		{
			if (state.receivewalWorkers[i].pid > 0 && kill(state.receivewalWorkers[i].pid,
														   0) == 0)
			{
				receivewalWorkersRunning++;
			}
		}
	}

	int bootstrapsPending = haveState ? state.bootstrapCount : 0;

	char uptime[32] = { 0 };

	format_uptime(haveState ? state.serveStartedAt : 0, uptime, sizeof(uptime));

	fformat(stdout, "pg_walserver: running (pid %d, uptime %s)\n", (int) servePid,
			uptime);
	fformat(stdout, "  receivewal workers: %d/%d running\n",
			receivewalWorkersRunning, receivewalPullCount);
	fformat(stdout, "  bootstrap backups pending: %d\n", bootstrapsPending);

	routes_free(routes);

	return true;
}
