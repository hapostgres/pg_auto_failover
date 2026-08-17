/*
 * src/bin/pg_autoctl/service_archiver_serve.c
 *   See service_archiver_serve.h.
 *
 * pg_walsender is exec'd exactly once per archiver process, and serves
 * every (formation, group) membership that archiver holds through the one
 * shared routes file (routes.h) -- one "[formation/group]" section per
 * membership, mapping the connection's dbname to that membership's own
 * local storage root. This file only knows how to fork+exec that one
 * process, matching service_archiver_capture_start()'s sibling shape
 * (service_archiver_run.c) and service_postgres_start()'s own
 * fork()-then-execv() pattern for the real Postgres child -- liveness
 * detection and restart-on-death are supervisor.c's job generically, the
 * same as for every other supervised child in this project, not this
 * file's own concern. It no longer builds or refreshes the routes file
 * itself -- service_archiver_reconciler.c does, at membership add/remove
 * time (the only two moments that mapping actually changes), and pg_
 * walsender resolves everything else (which base backup is current, this
 * group's system identifier, the current WAL position) by reading fresh,
 * purpose-built local files directly at connection time rather than
 * through any periodically-refreshed cache. See archiving-details.rst's
 * "Keeping local files current" section for the full rationale behind
 * this split.
 *
 * Deliberately monitor-independent: unlike service_archiver.c's own
 * capture loop, nothing in this file ever talks to the monitor, so pg_
 * walsender keeps serving already-captured data through a monitor outage
 * with zero risk of anything here getting stuck retrying a monitor call
 * instead of noticing pg_walsender died.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <unistd.h>

#include "service_archiver_serve.h"

#include "cli_root.h"           /* pg_autoctl_program */
#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "signals.h"

/* set by --port; 0 means "use PG_AUTOCTL_ARCHIVER_SERVE_PORT" */
static int archiverServePort = 0;


void
service_archiver_serve_set_port(int port)
{
	archiverServePort = port;
}


/*
 * service_archiver_walsender_start forks and execv's pg_walsender, in the
 * exact shape supervisor.c's Service.startFunction expects (see
 * service_postgres_start(), service_postgres.c, for the same pattern
 * against the real Postgres binary) -- supervisor_start()/
 * supervisor_add_service() own everything past this point: noticing
 * pg_walsender died, restarting it (RP_PERMANENT, the same policy every
 * other permanent archiver service already uses), and SIGTERM-ing it on
 * shutdown. No liveness polling or restart logic belongs in this file
 * anymore.
 */
bool
service_archiver_walsender_start(void *context, pid_t *pid)
{
	Keeper *keeper = (Keeper *) context;
	KeeperConfig *config = &(keeper->config);

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

	fflush(stdout);
	fflush(stderr);

	pid_t fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork pg_walsender: %m");
			return false;
		}

		case 0:
		{
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

		default:
		{
			log_debug("pg_autoctl started pg_walsender in subprocess %d", fpid);
			*pid = fpid;
			return true;
		}
	}
}
