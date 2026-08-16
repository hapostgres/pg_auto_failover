/*
 * src/bin/pg_autoctl/service_archiver_pgreceivewal_state.c
 *   See service_archiver_pgreceivewal_state.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <signal.h>

#include "postgres_fe.h"

#include "service_archiver_pgreceivewal_state.h"

#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "string_utils.h"


/*
 * archiver_pgreceivewal_state_path computes the path of the small desired-
 * state file pg_receivewal's own controller polls -- inside config->
 * pgSetup.pgdata itself (this membership's own walcache root), matching
 * every other per-membership bookkeeping file this project already keeps
 * there (archiver-position, archiver-systemid).
 */
void
archiver_pgreceivewal_state_path(KeeperConfig *config, char *dest)
{
	sformat(dest, MAXPGPATH, "%s/pgreceivewal.state", config->pgSetup.pgdata);
}


/*
 * archiver_pgreceivewal_pidfile_path computes the path of the small pidfile
 * pg_receivewal's own controller writes once its child is confirmed
 * started -- how service_archiver_pgreceivewal_ctl_is_running() answers
 * "is it alive" from a different process (the FSM tick) without needing
 * any IPC beyond the filesystem, matching pg_setup_is_ready()'s own
 * pidfile-based liveness check for ordinary Postgres.
 */
void
archiver_pgreceivewal_pidfile_path(KeeperConfig *config, char *dest)
{
	sformat(dest, MAXPGPATH, "%s/pgreceivewal.pid", config->pgSetup.pgdata);
}


/*
 * service_archiver_pgreceivewal_set_desired_state writes the desired-state
 * file pg_receivewal's own controller polls (service_archiver_
 * pgreceivewal_ctl.c) -- called by service_archiver.c's own fsm_init_
 * archiver/fsm_archiver_follow_new_primary/fsm_archiver_report_lsn entry
 * points (via service_archiver_start_pgreceivewal/service_archiver_stop_
 * pgreceivewal) instead of forking pg_receivewal directly. primaryNode
 * may be NULL when running is false (stopping doesn't need a target).
 */
bool
service_archiver_pgreceivewal_set_desired_state(Keeper *keeper,
												bool running,
												NodeAddress *primaryNode)
{
	KeeperConfig *config = &(keeper->config);
	char path[MAXPGPATH] = { 0 };

	archiver_pgreceivewal_state_path(config, path);

	char contents[BUFSIZE] = { 0 };
	int size;

	if (running && primaryNode != NULL)
	{
		char slotName[MAXCONNINFO] = { 0 };

		/* named after this archiver's own node id, the consumer -- matching
		 * keeper_create_and_drop_replication_slots()'s own naming for an
		 * ordinary standby (keeper.c, primary_standby.c, pgsql.c) */
		sformat(slotName, sizeof(slotName), "%s_%d",
				REPLICATION_SLOT_NAME_DEFAULT, keeper->state.current_node_id);

		size = sformat(contents, sizeof(contents),
					   "running = true\n"
					   "host = %s\n"
					   "port = %d\n"
					   "slot = %s\n",
					   primaryNode->host, primaryNode->port, slotName);
	}
	else
	{
		size = sformat(contents, sizeof(contents), "running = false\n");
	}

	return write_file_atomic(contents, size, path);
}


/*
 * archiver_pgreceivewal_read_desired_state reads the desired-state file
 * back. A missing file (the controller started before the FSM tick has
 * ever called service_archiver_pgreceivewal_set_desired_state() yet) is
 * not an error -- desired->running stays false, the safe default.
 */
bool
archiver_pgreceivewal_read_desired_state(KeeperConfig *config,
										 ArchiverPgReceivewalDesiredState *desired)
{
	memset(desired, 0, sizeof(ArchiverPgReceivewalDesiredState));

	char path[MAXPGPATH] = { 0 };

	archiver_pgreceivewal_state_path(config, path);

	if (!file_exists(path))
	{
		return true;
	}

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file(path, &contents, &fileSize) || contents == NULL)
	{
		/* transient read race with a concurrent atomic rewrite -- try
		 * again next poll, don't treat this as "stop everything" */
		return false;
	}

	char *lines[BUFSIZE] = { 0 };
	int lineCount = splitLines(contents, lines, BUFSIZE);

	for (int i = 0; i < lineCount; i++)
	{
		char key[64] = { 0 };
		char value[MAXCONNINFO] = { 0 };

		if (sscanf(lines[i], "%63s = %255s", key, value) != 2) /* IGNORE-BANNED */
		{
			continue;
		}

		if (streq(key, "running"))
		{
			desired->running = streq(value, "true");
		}
		else if (streq(key, "host"))
		{
			strlcpy(desired->host, value, sizeof(desired->host));
		}
		else if (streq(key, "port"))
		{
			stringToInt(value, &(desired->port));
		}
		else if (streq(key, "slot"))
		{
			strlcpy(desired->slot, value, sizeof(desired->slot));
		}
	}

	free(contents);

	return true;
}


/*
 * service_archiver_pgreceivewal_ctl_is_running answers "is pg_receivewal
 * currently running" for a reader in a different process (the FSM tick,
 * service_archiver.c) -- reads the controller's own pidfile and probes it
 * with kill(pid, 0), the same liveness pattern pg_setup_is_ready() uses
 * for ordinary Postgres. Never treats a missing/unreadable pidfile as an
 * error: "not running yet" is the correct, ordinary answer for it.
 */
bool
service_archiver_pgreceivewal_ctl_is_running(KeeperConfig *config, bool *isRunning)
{
	*isRunning = false;

	char path[MAXPGPATH] = { 0 };

	archiver_pgreceivewal_pidfile_path(config, path);

	if (!file_exists(path))
	{
		return true;
	}

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file(path, &contents, &fileSize) || contents == NULL)
	{
		return true;
	}

	int pid = 0;

	stringToInt(contents, &pid);
	free(contents);

	if (pid > 0 && kill((pid_t) pid, 0) == 0)
	{
		*isRunning = true;
	}

	return true;
}
