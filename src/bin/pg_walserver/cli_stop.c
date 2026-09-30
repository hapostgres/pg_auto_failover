/*
 * src/bin/pg_walserver/cli_stop.c
 *   See cli_stop.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "postgres_fe.h"

#include "commandline.h"

#include "cli_common.h"
#include "cli_root.h"
#include "cli_stop.h"
#include "log.h"
#include "pidfile.h"
#include "string_utils.h"

/* -----------------------------------------------------------------------
 * pg_walserver stop --pgdata <path>
 * ----------------------------------------------------------------------- */

static char stopPgdata[MAXPGPATH] = { 0 };

static struct option stopLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_stop_getopt parses "pg_walserver stop"'s own flags into the
 * file-scope statics above.
 */
static int
cli_stop_getopt(int argc, char **argv)
{
	optind = 0;
	stopPgdata[0] = '\0';
	ws_prefill_pgdata_from_env(stopPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:", stopLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(stopPgdata, optarg, sizeof(stopPgdata));
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
 * cli_stop_run sends SIGTERM to the "pg_walserver serve" instance whose
 * pid is recorded in <pgdata>/pg_walserver.pid -- the exact same shape as
 * "pg_ctl stop" (or, in this project's own vocabulary, "reload"'s own
 * cli_reload_run() just above, SIGTERM instead of SIGHUP): accept_loop.c's
 * own signal handler treats SIGTERM as a clean shutdown request (stop
 * accepting new connections, let in-flight ones finish, then exit), the
 * same disposition every other supervised process in this project already
 * gives it. Does not wait for the process to actually exit -- the signal
 * was delivered, that is this command's whole job, the same as "pg_ctl
 * stop -m fast" without "--wait" would be.
 */
static void
cli_stop_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	if (stopPgdata[0] == '\0')
	{
		log_fatal("stop requires --pgdata (or the PGDATA environment "
				  "variable)");
		exit(1);
	}

	char pidfilePath[MAXPGPATH];

	sformat(pidfilePath, sizeof(pidfilePath), "%s/pg_walserver.pid", stopPgdata);

	pid_t pid = 0;

	if (!read_pidfile(pidfilePath, &pid))
	{
		log_fatal("Failed to stop pg_walserver: no running instance found "
				  "at \"%s\" (missing, stale, or unreadable pidfile)",
				  pidfilePath);
		exit(1);
	}

	if (kill(pid, SIGTERM) != 0)
	{
		if (errno == ESRCH)
		{
			log_fatal("Failed to stop pg_walserver: pid %d (from \"%s\") "
					  "is not running", pid, pidfilePath);
		}
		else
		{
			log_fatal("Failed to send SIGTERM to pg_walserver pid %d: %m", pid);
		}
		exit(1);
	}

	log_info("Sent SIGTERM to pg_walserver pid %d", pid);
	exit(0);
}


CommandLine stop_command =
	make_command("stop",
				 "Stop a running pg_walserver cleanly",
				 "--pgdata <path>",
				 "  --pgdata    this instance's own top-level storage root "
				 "(defaults to\n"
				 "              PGDATA); sends SIGTERM to the pid recorded "
				 "in\n"
				 "              \"<pgdata>/pg_walserver.pid\"\n",
				 cli_stop_getopt, cli_stop_run);
