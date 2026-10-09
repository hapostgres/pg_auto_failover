/*
 * src/bin/pg_walserver/cli_status.c
 *   See cli_status.h.
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
#include "cli_root.h"
#include "cli_status.h"
#include "log.h"
#include "pidfile.h"
#include "ps_state.h"
#include "clusters.h"
#include "string_utils.h"

/* local helpers */
static void format_elapsed(time_t startedAt, char *dest, size_t destSize);

static int cli_status_getopt(int argc, char **argv);
static void cli_status_command_run(int argc, char **argv);


/* -----------------------------------------------------------------------
 * pg_walserver status --pgdata <path>
 * ----------------------------------------------------------------------- */

static char statusPgdata[MAXPGPATH] = { 0 };
static char statusConfigFile[MAXPGPATH] = { 0 };

static struct option statusLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ NULL, 0, NULL, 0 }
};

CommandLine status_command =
	make_command("status",
				 "Show a short pg_walserver status dashboard",
				 "--pgdata <path> [--config <path>]",
				 "  --pgdata    this instance's own top-level storage root "
				 "(defaults to\n"
				 "              PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n",
				 cli_status_getopt, cli_status_command_run);


/*
 * format_elapsed renders the time elapsed since startedAt via
 * IntervalToString() (common/string_utils.c), or "-" when startedAt is
 * unset (<= 0, "serve" isn't running) -- IntervalToString itself has no
 * such sentinel, so that case is handled here instead.
 */
static void
format_elapsed(time_t startedAt, char *dest, size_t destSize)
{
	if (startedAt <= 0)
	{
		strlcpy(dest, "-", destSize);
		return;
	}

	double elapsed = (double) (time(NULL) - startedAt);

	IntervalToString(elapsed < 0 ? 0 : elapsed, dest, destSize);
}


/*
 * cli_status_run prints the one-line/short dashboard described above.
 * Always returns true unless pgdata itself is missing -- "serve" not
 * running is a normal, cleanly reported case, never an error. configFile,
 * when given, overrides where the config file itself lives, independent
 * of pgdata -- see config_file_path()'s own comment, clusters.h.
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
	 * How many clusters are configured with "receivewal = pull", the
	 * denominator for the "N/M running" line below -- a process-workforce
	 * fact (how many supervised children are *supposed* to be running),
	 * not the data-layer facts (backup/WAL presence, which cluster is
	 * which) that :ref:`pg_walserver_ls`/:ref:`pg_walserver_list` already
	 * own; status is deliberately only ever about this process and its
	 * own child processes, never the archive data those processes
	 * maintain.
	 */
	char clustersPath[MAXPGPATH] = { 0 };

	config_file_path(pgdata, configFile, clustersPath, sizeof(clustersPath));

	WsCluster *clusters = NULL;
	int clusterCount = 0;
	int receivewalPullCount = 0;

	if (clusters_load(clustersPath, &clusters, &clusterCount))
	{
		for (int i = 0; i < clusterCount; i++)
		{
			if (clusters[i].receivewalPull)
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

	format_elapsed(haveState ? state.serveStartedAt : 0, uptime, sizeof(uptime));

	fformat(stdout, "pg_walserver: running (pid %d, uptime %s)\n", (int) servePid,
			uptime);
	fformat(stdout, "  receivewal workers: %d/%d running\n",
			receivewalWorkersRunning, receivewalPullCount);
	fformat(stdout, "  bootstrap backups pending: %d\n", bootstrapsPending);

	clusters_free(clusters);

	return true;
}


/*
 * cli_status_getopt parses "pg_walserver status"'s own flags into the
 * file-scope statics above.
 */
static int
cli_status_getopt(int argc, char **argv)
{
	optind = 0;
	ws_prefill_pgdata_from_env(statusPgdata);
	statusConfigFile[0] = '\0';

	int c;

	while ((c = getopt_long(argc, argv, "D:f:", statusLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(statusPgdata, optarg, sizeof(statusPgdata));
				break;
			}

			case 'f':
			{
				strlcpy(statusConfigFile, optarg, sizeof(statusConfigFile));
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
 * cli_status_command_run runs "pg_walserver status" against the options
 * cli_status_getopt parsed above, then exit()s with its own result.
 */
static void
cli_status_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(cli_status_run(statusPgdata, statusConfigFile) ? 0 : 1);
}
