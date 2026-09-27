/*
 * src/bin/pg_autoctl/service_archiver_pgreceivewal_state.c
 *   See service_archiver_pgreceivewal_state.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

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


/*
 * read_small_proc_file reads at most size-1 bytes of a /proc file (their
 * st_size is 0, so read_file() can't be used). Returns false when the file
 * can't be read (non-Linux, or the process is gone).
 */
static bool
read_small_proc_file(const char *path, char *buf, size_t size)
{
	int fd = open(path, O_RDONLY);

	if (fd < 0)
	{
		return false;
	}

	ssize_t n = read(fd, buf, size - 1);

	close(fd);

	if (n <= 0)
	{
		return false;
	}

	buf[n] = '\0';
	return true;
}


/*
 * archiver_pid_is_ours guards against pid reuse before a stale pid read
 * back from a pidfile is ever signalled: the process must (1) exist, (2)
 * have a command name (/proc/<pid>/comm, truncated to 15 chars by the
 * kernel) starting with expectedComm, and (3) have been started no later
 * than the pidfile's own mtime (a process started afterwards, with the
 * same pid, cannot be the one that was recorded). Anything we cannot
 * verify (no /proc) is treated as "not ours": never signal on a guess.
 */
bool
archiver_pid_is_ours(pid_t pid, const char *expectedComm, const char *pidfilePath)
{
	char path[64] = { 0 };
	char buf[1024] = { 0 };

	if (pid <= 1 || pid == getpid())
	{
		return false;
	}

	sformat(path, sizeof(path), "/proc/%d/comm", (int) pid);

	if (!read_small_proc_file(path, buf, sizeof(buf)))
	{
		return false;
	}

	buf[strcspn(buf, "\n")] = '\0';

	size_t commLen = Min(strlen(expectedComm), (size_t) 15);

	if (strncmp(buf, expectedComm, commLen) != 0)
	{
		return false;
	}

	struct stat pidfileStat;

	if (pidfilePath == NULL || stat(pidfilePath, &pidfileStat) != 0)
	{
		return false;
	}

	/* process start time: field 22 of /proc/<pid>/stat, after the ")" */
	sformat(path, sizeof(path), "/proc/%d/stat", (int) pid);

	if (!read_small_proc_file(path, buf, sizeof(buf)))
	{
		return false;
	}

	char *p = strrchr(buf, ')');

	if (p == NULL)
	{
		return false;
	}

	p++;

	/* p now starts at field 3 (state); starttime is field 22 */
	for (int field = 3; field < 22; field++)
	{
		while (*p == ' ')
		{
			p++;
		}
		while (*p != '\0' && *p != ' ')
		{
			p++;
		}
	}

	unsigned long long startTicks = strtoull(p, NULL, 10); /* IGNORE-BANNED */

	char statBuf[4096] = { 0 };

	if (!read_small_proc_file("/proc/stat", statBuf, sizeof(statBuf)))
	{
		return false;
	}

	char *btime = strstr(statBuf, "btime ");

	if (btime == NULL)
	{
		return false;
	}

	unsigned long long bootTime = strtoull(btime + 6, NULL, 10); /* IGNORE-BANNED */
	long ticks = sysconf(_SC_CLK_TCK);

	if (ticks <= 0)
	{
		return false;
	}

	unsigned long long startedAt = bootTime + startTicks / (unsigned long long) ticks;

	return startedAt <= (unsigned long long) pidfileStat.st_mtime + 2;
}


/*
 * archiver_stop_stale_pid signals a process that is NOT our child (so it
 * can't be waitpid()ed): firstSignal, then a bounded poll of kill(pid, 0),
 * then SIGKILL.
 */
void
archiver_stop_stale_pid(pid_t pid, int firstSignal, int timeoutMs)
{
	if (kill(pid, firstSignal) != 0)
	{
		return;
	}

	for (int waited = 0; waited < timeoutMs; waited += 100)
	{
		if (kill(pid, 0) != 0)
		{
			return;
		}

		pg_usleep(100 * 1000);
	}

	log_warn("Leftover process %d did not stop in %dms, sending SIGKILL",
			 (int) pid, timeoutMs);

	(void) kill(pid, SIGKILL);
}


/*
 * archiver_pgreceivewal_stop_stale_child stops a pg_receivewal process
 * recorded in this membership's pidfile by a previous controller instance
 * that died without stopping it -- after verifying the pid is really ours.
 */
void
archiver_pgreceivewal_stop_stale_child(KeeperConfig *config)
{
	char path[MAXPGPATH] = { 0 };

	archiver_pgreceivewal_pidfile_path(config, path);

	if (!file_exists(path))
	{
		return;
	}

	char *contents = NULL;
	long fileSize = 0;

	if (read_file(path, &contents, &fileSize) && contents != NULL)
	{
		int pid = 0;

		if (stringToInt(contents, &pid) && pid > 0)
		{
			if (archiver_pid_is_ours((pid_t) pid, "pg_autoctl", path))
			{
				log_info("Stopping leftover pg_receivewal process %d from a "
						 "previous controller instance", pid);

				archiver_stop_stale_pid((pid_t) pid, SIGINT, 10000);
			}
			else
			{
				log_debug("Ignoring stale pidfile entry %d in \"%s\": not "
						  "a pg_autoctl process of ours", pid, path);
			}
		}

		free(contents);
	}

	(void) unlink_file(path);
}
