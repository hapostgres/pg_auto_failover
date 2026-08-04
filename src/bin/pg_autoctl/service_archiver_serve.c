/*
 * src/bin/pg_autoctl/service_archiver_serve.c
 *   See service_archiver_serve.h.
 *
 * Milestone 2's own scope, per the Build order in
 * ~/dev/temp/archiving-disaster-recovery.md: pg_walsender is exec'd exactly
 * once per archiver process, matching service_archiver.c's own single-
 * membership scope for pg_receivewal -- a future milestone generalizing to
 * several (formation, group) memberships per archiver needs this file and
 * service_archiver.c to grow the same "one child per membership" model
 * together, not independently.
 *
 * The routes file this writes is deliberately built from *local* config
 * (config->formation/groupId/pgSetup.pgdata), not a monitor round-trip:
 * archiver_add_formation()'s own SQL (pgautofailover.sql) inserts the new
 * archiver_node row's pgdata as an empty string -- the monitor has no way
 * to know an archiver's local WAL cache path, that's inherently
 * archiver-host-local information never sent to it. The one thing genuinely
 * worth asking the monitor is the latest base backup's storage location
 * (monitor_get_latest_basebackup_info), which is real, monitor-tracked
 * state once the "Base backup generation" milestone lands.
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
#include "monitor.h"
#include "signals.h"

/* how often service_archiver_serve_loop() re-checks pg_walsender's
 * liveness and refreshes the routes file, in seconds */
#define ARCHIVER_SERVE_TICK_SECONDS 1
#define ARCHIVER_SERVE_ROUTES_REFRESH_TICKS 30

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


static void
service_archiver_serve_routes_path(KeeperConfig *config, char *dest)
{
	path_in_same_directory(config->pathnames.config,
						   "archiver-routes.ini", dest);
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

	char routesPath[MAXPGPATH] = { 0 };

	service_archiver_serve_routes_path(config, routesPath);

	int port = archiverServePort > 0 ? archiverServePort : PG_AUTOCTL_ARCHIVER_SERVE_PORT;
	char portStr[16] = { 0 };

	sformat(portStr, sizeof(portStr), "%d", port);

	log_info("Starting pg_walsender on port %d, routes \"%s\"", port, routesPath);

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
		args[argsIndex++] = "--routes";
		args[argsIndex++] = routesPath;
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
service_archiver_serve_refresh_routes(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);

	char basebackupLocation[MAXPGPATH] = { 0 };
	char basebackupSource[NAMEDATALEN] = { 0 };
	bool found = false;

	if (!monitor_get_latest_basebackup_info(&(keeper->monitor),
											config->formation,
											config->groupId,
											basebackupLocation,
											sizeof(basebackupLocation),
											basebackupSource,
											sizeof(basebackupSource),
											&found))
	{
		log_warn("Failed to fetch the latest base backup location from the "
				 "monitor; the routes file will omit it for now");
		found = false;
	}

	uint64_t systemIdentifier = 0;
	bool foundSystemIdentifier = false;

	if (!monitor_get_group_system_identifier(&(keeper->monitor),
											 config->formation,
											 config->groupId,
											 &systemIdentifier,
											 &foundSystemIdentifier))
	{
		log_warn("Failed to fetch the group's system identifier from the "
				 "monitor; the routes file will omit it for now");
		foundSystemIdentifier = false;
	}

	char routesPath[MAXPGPATH] = { 0 };

	service_archiver_serve_routes_path(config, routesPath);

	char tmpPath[MAXPGPATH] = { 0 };

	sformat(tmpPath, sizeof(tmpPath), "%s.tmp", routesPath);

	FILE *fileStream = fopen_with_umask(tmpPath, "w", FOPEN_FLAGS_W, 0644);

	if (fileStream == NULL)
	{
		/* errors have already been logged */
		return false;
	}

	fformat(fileStream, "[%s/%d]\n", config->formation, config->groupId);
	fformat(fileStream, "walcache = %s\n", config->pgSetup.pgdata);

	if (found)
	{
		fformat(fileStream, "basebackup = %s\n", basebackupLocation);
	}

	if (foundSystemIdentifier)
	{
		fformat(fileStream, "systemid = %" PRIu64 "\n", systemIdentifier);
	}

	if (fclose(fileStream) == EOF)
	{
		log_error("Failed to write file \"%s\": %m", tmpPath);
		return false;
	}

	if (rename(tmpPath, routesPath) != 0)
	{
		log_error("Failed to rename \"%s\" to \"%s\": %m", tmpPath, routesPath);
		return false;
	}

	log_debug("Refreshed archiver routes file \"%s\"", routesPath);

	return true;
}


bool
service_archiver_serve_loop(Keeper *keeper)
{
	log_info("pg_autoctl archiver serve: archiver %" PRId64 ", formation "
															"\"%s\", group %d",
			 keeper->config.archiverId, keeper->config.formation,
			 keeper->config.groupId);

	if (!service_archiver_serve_refresh_routes(keeper))
	{
		log_warn("Failed to write the initial routes file; pg_walsender "
				 "will start without one route resolved yet");
	}

	if (!service_archiver_serve_start_walsender(keeper))
	{
		log_fatal("Failed to start pg_walsender, see above for details");
		return false;
	}

	int tickCount = 0;

	for (;;)
	{
		if (asked_to_stop || asked_to_stop_fast || asked_to_quit)
		{
			break;
		}

		if (asked_to_reload)
		{
			asked_to_reload = 0;
			(void) service_archiver_serve_refresh_routes(keeper);
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

		if (tickCount > 0 &&
			tickCount % ARCHIVER_SERVE_ROUTES_REFRESH_TICKS == 0)
		{
			(void) service_archiver_serve_refresh_routes(keeper);
		}

		sleep(ARCHIVER_SERVE_TICK_SECONDS);
		++tickCount;
	}

	(void) service_archiver_serve_stop_walsender();

	return true;
}
