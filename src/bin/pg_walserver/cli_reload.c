/*
 * src/bin/pg_walserver/cli_reload.c
 *   See cli_reload.h.
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
#include "cli_reload.h"
#include "cli_root.h"
#include "log.h"
#include "pidfile.h"
#include "string_utils.h"

/* -----------------------------------------------------------------------
 * pg_walserver reload --pgdata <path>
 * ----------------------------------------------------------------------- */

static char reloadPgdata[MAXPGPATH] = { 0 };

static struct option reloadLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ NULL, 0, NULL, 0 }
};

static int cli_reload_getopt(int argc, char **argv);
static void cli_reload_run(int argc, char **argv);

CommandLine reload_command =
	make_command("reload",
				 "Ask a running pg_walserver to reload its configuration",
				 "--pgdata <path>",
				 "  --pgdata    this instance's own top-level storage root "
				 "(defaults to\n"
				 "              PGDATA); sends SIGHUP to the pid recorded "
				 "in\n"
				 "              \"<pgdata>/pg_walserver.pid\"\n",
				 cli_reload_getopt, cli_reload_run);


/*
 * cli_reload_getopt parses "pg_walserver reload"'s own flags into the
 * file-scope statics above.
 */
static int
cli_reload_getopt(int argc, char **argv)
{
	optind = 0;
	reloadPgdata[0] = '\0';
	ws_prefill_pgdata_from_env(reloadPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:", reloadLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(reloadPgdata, optarg, sizeof(reloadPgdata));
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
 * cli_reload_run sends SIGHUP to the "pg_walserver serve" instance whose
 * pid is recorded in <pgdata>/pg_walserver.pid -- the exact same shape as
 * "pg_ctl reload". Follows pg_autoctl's own reload precedent (cli_common.c's
 * cli_pg_autoctl_reload(), cli_service.c's cli_service_reload()): SIGHUP is
 * ignored in THIS process first, before sending it on, because a freshly
 * exec'd one-shot command like this one installs no SIGHUP handler of its
 * own, and can end up reusing the pid of a just-exited process -- the
 * default disposition for an unhandled SIGHUP is to terminate, so without
 * this a stray signal delivered to that reused pid in the narrow window
 * before this command exits could kill it before it ever sends anything.
 * read_pidfile() (src/bin/common/pidfile.h) already does the missing/
 * stale-pidfile detection (a kill(pid, 0) check, removing a stale file);
 * exits 0 once SIGHUP was actually delivered, nonzero with a clear error
 * otherwise.
 */
static void
cli_reload_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	if (reloadPgdata[0] == '\0')
	{
		log_fatal("reload requires --pgdata (or the PGDATA environment "
				  "variable)");
		exit(1);
	}

	char pidfilePath[MAXPGPATH];

	sformat(pidfilePath, sizeof(pidfilePath), "%s/pg_walserver.pid", reloadPgdata);

	signal(SIGHUP, SIG_IGN);

	pid_t pid = 0;

	if (!read_pidfile(pidfilePath, &pid))
	{
		log_fatal("Failed to reload pg_walserver: no running instance found "
				  "at \"%s\" (missing, stale, or unreadable pidfile)",
				  pidfilePath);
		exit(1);
	}

	if (kill(pid, SIGHUP) != 0)
	{
		if (errno == ESRCH)
		{
			log_fatal("Failed to reload pg_walserver: pid %d (from \"%s\") "
					  "is not running", pid, pidfilePath);
		}
		else
		{
			log_fatal("Failed to send SIGHUP to pg_walserver pid %d: %m", pid);
		}
		exit(1);
	}

	log_info("Sent SIGHUP to pg_walserver pid %d", pid);
	exit(0);
}
