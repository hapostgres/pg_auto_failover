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
#include "file_utils.h"
#include "log.h"
#include "pidfile.h"
#include "ps_state.h"
#include "routes.h"
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
cli_status_run(const char *pgdata)
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
		printf("pg_walserver: not running (--pgdata \"%s\")\n", pgdata); /* IGNORE-BANNED */
		return true;
	}

	/* how many routes are configured, and how many have "receivewal = pull" */
	char routesPath[MAXPGPATH] = { 0 };

	sformat(routesPath, sizeof(routesPath), "%s/pg_walserver.ini", pgdata);

	WsRoute *routes = NULL;
	int routeCount = 0;
	int receivewalPullCount = 0;
	int backupCount = 0;

	if (routes_load(routesPath, &routes, &routeCount))
	{
		for (int i = 0; i < routeCount; i++)
		{
			if (routes[i].receivewalPull)
			{
				receivewalPullCount++;
			}

			char latestPath[MAXPGPATH] = { 0 };

			sformat(latestPath, sizeof(latestPath), "%s/basebackups/.latest",
					routes[i].path);

			if (file_exists(latestPath))
			{
				backupCount++;
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

	printf("pg_walserver: running (pid %d, uptime %s)\n", (int) servePid, uptime); /* IGNORE-BANNED */
	printf("  clusters:  %d configured, %d with a base backup\n", /* IGNORE-BANNED */
		   routeCount, backupCount);
	printf("  receivewal workers: %d/%d running\n", /* IGNORE-BANNED */
		   receivewalWorkersRunning, receivewalPullCount);
	printf("  bootstrap backups pending: %d\n", bootstrapsPending); /* IGNORE-BANNED */

	routes_free(routes);

	return true;
}
