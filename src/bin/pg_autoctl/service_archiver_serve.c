/*
 * src/bin/pg_autoctl/service_archiver_serve.c
 *   See service_archiver_serve.h.
 *
 * pg_walsender is exec'd exactly once per archiver process, and serves
 * every (formation, group) membership that archiver holds through the one
 * shared routes file (routes.h) -- one "[formation/group]" section per
 * membership, mapping the connection's dbname to that membership's own
 * local storage root. This process owns exec'ing and supervising pg_
 * walsender's liveness only; it no longer builds or refreshes the routes
 * file itself -- service_archiver_reconciler.c does, at membership add/
 * remove time (the only two moments that mapping actually changes), and
 * pg_walsender resolves everything else (which base backup is current,
 * this group's system identifier, the current WAL position) by reading
 * fresh, purpose-built local files directly at connection time rather than
 * through any periodically-refreshed cache. See archiving-details.rst's
 * "Keeping local files current" section for the full rationale behind
 * this split.
 *
 * Deliberately monitor-independent: unlike service_archiver.c's own
 * capture loop, nothing in this file ever talks to the monitor, so pg_
 * walsender keeps serving already-captured data through a monitor outage
 * with zero risk of this supervisor itself getting stuck retrying a
 * monitor call instead of noticing pg_walsender died.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "service_archiver_serve.h"

#include "cli_root.h"           /* pg_autoctl_program */
#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "signals.h"

/* how often service_archiver_serve_loop() re-checks pg_walsender's
 * liveness, in seconds */
#define ARCHIVER_SERVE_TICK_SECONDS 1

/*
 * One pg_walsender child per archiver process, matching service_archiver.
 * c's own single-membership scope (see this file's own header comment).
 */
static pid_t pgWalsenderPid = -1;
static int archiverServePort = 0;


void
service_archiver_serve_set_port(int port)
{
	archiverServePort = port;
}


bool
service_archiver_serve_walsender_is_running(void)
{
	if (pgWalsenderPid <= 0)
	{
		return false;
	}

	int status = 0;
	pid_t ret = waitpid(pgWalsenderPid, &status, WNOHANG);

	if (ret == 0)
	{
		/* still running */
		return true;
	}

	if (ret == pgWalsenderPid)
	{
		log_warn("pg_walsender (pid %d) exited", pgWalsenderPid);
	}
	else if (ret == -1 && errno != ECHILD)
	{
		log_warn("Failed to waitpid() on pg_walsender (pid %d): %m", pgWalsenderPid);
	}

	pgWalsenderPid = -1;
	return false;
}


bool
service_archiver_serve_stop_walsender(void)
{
	if (pgWalsenderPid <= 0)
	{
		return true;
	}

	log_info("Stopping pg_walsender (pid %d)", pgWalsenderPid);

	if (kill(pgWalsenderPid, SIGTERM) != 0 && errno != ESRCH)
	{
		log_error("Failed to send SIGTERM to pg_walsender (pid %d): %m",
				  pgWalsenderPid);
		return false;
	}

	int status = 0;

	if (waitpid(pgWalsenderPid, &status, 0) == -1 && errno != ECHILD)
	{
		log_error("Failed to waitpid() on pg_walsender (pid %d): %m",
				  pgWalsenderPid);
		pgWalsenderPid = -1;
		return false;
	}

	pgWalsenderPid = -1;
	return true;
}


bool
service_archiver_serve_start_walsender(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);

	if (!service_archiver_serve_stop_walsender())
	{
		/* errors have already been logged */
		return false;
	}

	char pgWalsenderPath[MAXPGPATH] = { 0 };

	path_in_same_directory(pg_autoctl_program, "pg_walsender", pgWalsenderPath);

	if (!file_exists(pgWalsenderPath))
	{
		log_error("Failed to find pg_walsender at \"%s\"", pgWalsenderPath);
		return false;
	}

	int port = archiverServePort > 0 ? archiverServePort : PG_AUTOCTL_ARCHIVER_SERVE_PORT;
	char portStr[16] = { 0 };

	sformat(portStr, sizeof(portStr), "%d", port);

	log_info("Starting pg_walsender on port %d, pgdata \"%s\"",
			 port, config->pgSetup.pgdata);

	pid_t pid = fork();

	if (pid == -1)
	{
		log_error("Failed to fork pg_walsender: %m");
		return false;
	}

	if (pid == 0)
	{
		/* child process: replace ourselves with pg_walsender */
		char *args[6];
		int argsIndex = 0;

		args[argsIndex++] = pgWalsenderPath;
		args[argsIndex++] = "--port";
		args[argsIndex++] = portStr;
		args[argsIndex++] = "--pgdata";
		args[argsIndex++] = config->pgSetup.pgdata;
		args[argsIndex] = NULL;

		execv(pgWalsenderPath, args);

		/* execv only returns on failure */
		log_fatal("execv(\"%s\"): %m", pgWalsenderPath);
		_exit(127);
	}

	/* parent process: track the child, keep running our own loop */
	pgWalsenderPid = pid;

	return true;
}


bool
service_archiver_serve_loop(Keeper *keeper)
{
	log_info("pg_autoctl archiver serve: archiver %" PRId64 ", formation "
															"\"%s\", group %d",
			 keeper->config.archiverId, keeper->config.formation,
			 keeper->config.groupId);

	if (!service_archiver_serve_start_walsender(keeper))
	{
		log_fatal("Failed to start pg_walsender, see above for details");
		return false;
	}

	for (;;)
	{
		if (asked_to_stop || asked_to_stop_fast || asked_to_quit)
		{
			break;
		}

		if (!service_archiver_serve_walsender_is_running())
		{
			log_warn("pg_walsender is not running anymore, restarting it");

			if (!service_archiver_serve_start_walsender(keeper))
			{
				log_error("Failed to restart pg_walsender, will retry on "
						  "the next tick");
			}
		}

		sleep(ARCHIVER_SERVE_TICK_SECONDS);
	}

	(void) service_archiver_serve_stop_walsender();

	return true;
}
