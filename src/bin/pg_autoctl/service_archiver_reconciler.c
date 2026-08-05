/*
 * src/bin/pg_autoctl/service_archiver_reconciler.c
 *   Archiving & Disaster Recovery: an archiver's own membership
 *   reconciler.
 *
 * One archiver identity can hold more than one (formation, group)
 * membership at once -- every group of a Citus formation, or several
 * unrelated formations altogether. Each membership needs its own WAL
 * capture (service_archiver.c's service_archiver_loop(), one
 * pg_receivewal per membership) and, through it, its own base-backup
 * scheduling -- but only ever one shared pg_walsender/routes file
 * (service_archiver_serve.c already multiplexes every membership's own
 * data from a single process).
 *
 * This file is the intermediate supervised process (started by
 * start_archiver(), service_archiver_run.c, alongside "serve") whose one
 * job is to keep the set of running per-membership capture processes in
 * sync with what the monitor currently reports this archiver is attached
 * to -- discovered via pgautofailover.list_archiver_memberships(),
 * diffed against supervisor.c's own dynamic Service array
 * (supervisor_add_service()/supervisor_remove_service()) on a periodic
 * tick.
 *
 * Living as its own intermediate process, rather than folding this
 * directly into start_archiver()'s own top-level supervisor, is a
 * deliberate blast-radius choice: a bug in this genuinely new dynamic-
 * reconciliation logic can only crash and restart this one process (via
 * the top-level supervisor's own plain, unmodified RP_PERMANENT restart
 * policy) -- "serve" keeps running throughout, unaffected, still able to
 * serve whatever every membership already has captured on disk.
 *
 * Crash recovery: if this process itself is restarted, its own in-memory
 * record of which pid belongs to which membership is gone -- but the
 * capture processes it had started are not (a crashed parent doesn't
 * kill its children). Blindly re-discovering memberships and starting a
 * fresh capture for each would risk two pg_receivewal processes fighting
 * over the same replication slot. Rather than trying to adopt those
 * still-running orphans (subtle, and a running pg_receivewal restarting
 * against its own slot is already a safe, ordinary occurrence elsewhere
 * in this project), this file takes the simpler and equally safe path:
 * on startup, read back a small persisted "pid formation group" tracking
 * file, SIGTERM anything in it that is still alive, and then start every
 * currently-discovered membership fresh. A replication slot retains WAL
 * back to its own restart_lsn regardless of how many times its consumer
 * reconnects, so this brief, deliberate restart costs nothing.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "service_archiver_reconciler.h"

#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "monitor.h"
#include "service_archiver_run.h"
#include "signals.h"
#include "state.h"
#include "string_utils.h"
#include "supervisor.h"

/*
 * How often the reconciler actually re-queries the monitor for this
 * archiver's current membership list. The periodic callback itself may
 * be invoked much more often than this by supervisor_loop() (as often as
 * every 100ms when otherwise idle) -- this is a wall-clock gate on top
 * of that, not a tick count, so it stays correct regardless of how fast
 * the underlying loop happens to be running.
 */
#define ARCHIVER_RECONCILER_INTERVAL_SECONDS 30

/*
 * The name every reconciler-managed capture service is given, so this
 * file's own diffing logic can tell a capture service apart from
 * anything else that might end up on the same supervisor (there is
 * nothing else on this one today, but matching the prefix rather than
 * assuming makes that an explicit invariant instead of an accident).
 */
#define ARCHIVER_CAPTURE_SERVICE_NAME_PREFIX "archiver-capture-"


static char * archiver_reconciler_tracking_path(Keeper *templateKeeper, char *dest);
static char * archiver_reconciler_pidfile_path(Keeper *templateKeeper, char *dest);
static void archiver_reconciler_cleanup_stale_children(Keeper *templateKeeper);
static bool archiver_reconciler_write_tracking_file(Keeper *templateKeeper,
													Supervisor *supervisor);
static bool build_membership_keeper(Keeper *templateKeeper,
									ArchiverMembership *membership,
									Keeper **outKeeper);
static bool membership_service_name(ArchiverMembership *membership,
									char *dest, size_t destSize);
static bool find_membership_service(Supervisor *supervisor,
									const char *formation, int groupId,
									Service **result);
static void archiver_reconciler_tick(Supervisor *supervisor, void *context);


/*
 * archiver_reconciler_tracking_path computes the path of the small
 * "pid formation group" tracking file this reconciler persists across
 * its own restarts, one line per currently-managed capture child --
 * sibling of the archiver's own pg_autoctl.cfg, matching every other
 * archiver-root-level bookkeeping file (archiver-routes.ini) this
 * project already writes there.
 */
static char *
archiver_reconciler_tracking_path(Keeper *templateKeeper, char *dest)
{
	path_in_same_directory(templateKeeper->config.pathnames.config,
						   "archiver-reconciler-children", dest);
	return dest;
}


/*
 * archiver_reconciler_pidfile_path computes the pidfile this
 * reconciler's own inner supervisor_start_with_callback() call tracks
 * its capture children with -- distinct from templateKeeper->config.
 * pathnames.pid, which belongs to start_archiver()'s own top-level
 * supervisor (tracking "serve" and this reconciler process itself):
 * both are real, independently-owned pidfiles, and must never collide
 * on the same path.
 */
static char *
archiver_reconciler_pidfile_path(Keeper *templateKeeper, char *dest)
{
	path_in_same_directory(templateKeeper->config.pathnames.pid,
						   "archiver-reconciler.pid", dest);
	return dest;
}


/*
 * archiver_reconciler_cleanup_stale_children reads back the tracking
 * file left behind by a previous instance of this same process (if any)
 * and SIGTERMs any pid still alive -- see this file's own header comment
 * for why a clean restart, rather than adoption, is the deliberate
 * choice here. Best effort throughout: a missing file, an unparsable
 * line, or a signal failure (the process was already gone) are all
 * simply skipped, never fatal -- whatever is genuinely still running
 * gets caught by the fresh discovery pass that follows regardless.
 */
static void
archiver_reconciler_cleanup_stale_children(Keeper *templateKeeper)
{
	char path[MAXPGPATH] = { 0 };

	(void) archiver_reconciler_tracking_path(templateKeeper, path);

	if (!file_exists(path))
	{
		return;
	}

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file(path, &contents, &fileSize) || contents == NULL)
	{
		return;
	}

	char *lines[BUFSIZE] = { 0 };
	int lineCount = splitLines(contents, lines, BUFSIZE);

	for (int i = 0; i < lineCount; i++)
	{
		int pid = 0;
		char formation[NAMEDATALEN] = { 0 };
		int groupId = 0;

		if (sscanf(lines[i], "%d %63s %d", /* IGNORE-BANNED */
				   &pid, formation, &groupId) != 3)
		{
			continue;
		}

		if (pid <= 0)
		{
			continue;
		}

		if (kill((pid_t) pid, 0) == 0)
		{
			log_info("Stopping leftover archiver capture process %d for "
					 "\"%s\"/%d from a previous reconciler instance",
					 pid, formation, groupId);

			if (kill((pid_t) pid, SIGTERM) != 0)
			{
				log_warn("Failed to signal leftover process %d: %m", pid);
			}
		}
	}

	free(contents);
}


/*
 * archiver_reconciler_write_tracking_file persists the current set of
 * reconciler-managed capture services, atomically (write to a .tmp path,
 * then rename) so a concurrent reader (this same process, on its own
 * next restart) never observes a partial write.
 */
static bool
archiver_reconciler_write_tracking_file(Keeper *templateKeeper,
										Supervisor *supervisor)
{
	char path[MAXPGPATH] = { 0 };

	(void) archiver_reconciler_tracking_path(templateKeeper, path);

	char tmpPath[MAXPGPATH] = { 0 };

	sformat(tmpPath, sizeof(tmpPath), "%s.tmp", path);

	FILE *fileStream = fopen_with_umask(tmpPath, "w", FOPEN_FLAGS_W, 0644);

	if (fileStream == NULL)
	{
		/* errors have already been logged */
		return false;
	}

	for (int i = 0; i < supervisor->serviceCount; i++)
	{
		Service *service = &(supervisor->services[i]);

		if (strncmp(service->name, ARCHIVER_CAPTURE_SERVICE_NAME_PREFIX,
					strlen(ARCHIVER_CAPTURE_SERVICE_NAME_PREFIX)) != 0)
		{
			continue;
		}

		Keeper *membershipKeeper = (Keeper *) service->context;

		fformat(fileStream, "%d %s %d\n",
				service->pid,
				membershipKeeper->config.formation,
				membershipKeeper->config.groupId);
	}

	if (fclose(fileStream) == EOF)
	{
		log_warn("Failed to write file \"%s\": %m", tmpPath);
		return false;
	}

	if (rename(tmpPath, path) != 0)
	{
		log_warn("Failed to rename \"%s\" to \"%s\": %m", tmpPath, path);
		return false;
	}

	return true;
}


/*
 * build_membership_keeper builds a full, independent Keeper for one
 * membership out of the shared archiver-level template Keeper: the
 * archiver identity (archiverId, monitor_pguri, pg_ctl, hostname, name,
 * ...) is copied as-is, while formation/groupId and pgSetup.pgdata are
 * overridden to this membership's own values -- a dedicated
 * <root>/<formation>/<group>/ subdirectory, created here if missing,
 * that becomes this membership's own WAL cache, basebackups/, and (via
 * keeper_config_set_pathnames_from_pgdata below, which derives every
 * pathname from pgdata) its own state/config/pid file paths, entirely
 * distinct from every other membership's.
 *
 * Every existing service_archiver_ or service_archiver_basebackup_
 * function already takes this same Keeper / KeeperConfig shape and
 * derives everything it does from config->formation/groupId/pgSetup.
 * pgdata -- so building N of these and forking one capture child per
 * instance (service_archiver_capture_start(), unchanged) is what makes
 * multi-membership support possible without touching that code at all.
 *
 * The returned Keeper is heap-allocated and becomes the long-lived
 * Service.context for its own capture child -- owned by the caller from
 * here on (freed on removal, see archiver_reconciler_tick()).
 */
static bool
build_membership_keeper(Keeper *templateKeeper, ArchiverMembership *membership,
						Keeper **outKeeper)
{
	Keeper *membershipKeeper = (Keeper *) calloc(1, sizeof(Keeper));

	if (membershipKeeper == NULL)
	{
		log_error("Failed to allocate memory for archiver membership "
				  "\"%s\"/%d", membership->formation, membership->groupId);
		return false;
	}

	/* start from a shallow copy of the shared archiver identity -- every
	 * field is a plain value (char arrays, ints), never a pointer this
	 * process doesn't already own, so a shallow copy is a real copy */
	*membershipKeeper = *templateKeeper;

	strlcpy(membershipKeeper->config.formation, membership->formation,
			sizeof(membershipKeeper->config.formation));
	membershipKeeper->config.groupId = membership->groupId;

	sformat(membershipKeeper->config.pgSetup.pgdata,
			sizeof(membershipKeeper->config.pgSetup.pgdata),
			"%s/%s/%d",
			templateKeeper->config.pgSetup.pgdata,
			membership->formation, membership->groupId);

	if (!directory_exists(membershipKeeper->config.pgSetup.pgdata) &&
		pg_mkdir_p(membershipKeeper->config.pgSetup.pgdata, 0700) != 0)
	{
		log_error("Failed to create archiver membership directory \"%s\": %m",
				  membershipKeeper->config.pgSetup.pgdata);
		free(membershipKeeper);
		return false;
	}

	/*
	 * The shallow copy above inherited the template keeper's own
	 * already-computed pathnames (config/state/nodes/pid, derived from
	 * the archiver-level pgdata). keeper_config_set_pathnames_from_pgdata()'s
	 * setters each skip an already-nonempty field, so without this reset
	 * every membership beyond the first would silently keep pointing at
	 * the template's (or an earlier membership's) files instead of its
	 * own -- clear them so they're recomputed from this membership's own
	 * pgdata below.
	 */
	memset(&(membershipKeeper->config.pathnames), 0,
		   sizeof(membershipKeeper->config.pathnames));

	if (!keeper_config_set_pathnames_from_pgdata(
			&(membershipKeeper->config.pathnames),
			membershipKeeper->config.pgSetup.pgdata))
	{
		log_error("Failed to compute pathnames for archiver membership "
				  "\"%s\"/%d", membership->formation, membership->groupId);
		free(membershipKeeper);
		return false;
	}

	/*
	 * Only lay down initial state the first time this membership is ever
	 * captured (no state file yet) -- service_archiver_loop()'s own
	 * first action every tick is keeper_load_state(), so an already-
	 * existing file (this membership was captured before, e.g. across an
	 * archiver restart) must be left alone rather than clobbered back to
	 * this tick's snapshot of reported/goal state.
	 */
	if (!file_exists(membershipKeeper->config.pathnames.state))
	{
		keeper_state_init(&(membershipKeeper->state));
		membershipKeeper->state.current_node_id = membership->nodeId;
		membershipKeeper->state.current_group = membership->groupId;
		membershipKeeper->state.current_role = membership->reportedState;
		membershipKeeper->state.assigned_role = membership->goalState;

		if (!keeper_store_state(membershipKeeper))
		{
			log_error("Failed to write initial state for archiver "
					  "membership \"%s\"/%d", membership->formation,
					  membership->groupId);
			free(membershipKeeper);
			return false;
		}
	}

	*outKeeper = membershipKeeper;
	return true;
}


/*
 * membership_service_name computes the supervised-service name for one
 * membership's capture process -- ARCHIVER_CAPTURE_SERVICE_NAME_PREFIX
 * plus "<formation>-<group>", what this file's own diffing and tracking-
 * file logic key on.
 */
static bool
membership_service_name(ArchiverMembership *membership, char *dest, size_t destSize)
{
	sformat(dest, destSize, "%s%s-%d", ARCHIVER_CAPTURE_SERVICE_NAME_PREFIX,
			membership->formation, membership->groupId);
	return true;
}


/*
 * find_membership_service looks for an already-supervised capture
 * service for (formation, groupId) among supervisor->services, matching
 * on each service's own Keeper context rather than re-parsing its name.
 */
static bool
find_membership_service(Supervisor *supervisor, const char *formation,
						int groupId, Service **result)
{
	for (int i = 0; i < supervisor->serviceCount; i++)
	{
		Service *service = &(supervisor->services[i]);

		if (strncmp(service->name, ARCHIVER_CAPTURE_SERVICE_NAME_PREFIX,
					strlen(ARCHIVER_CAPTURE_SERVICE_NAME_PREFIX)) != 0)
		{
			continue;
		}

		Keeper *membershipKeeper = (Keeper *) service->context;

		if (streq(membershipKeeper->config.formation, formation) &&
			membershipKeeper->config.groupId == groupId)
		{
			*result = service;
			return true;
		}
	}

	return false;
}


/*
 * archiver_reconciler_tick is this file's own Supervisor.periodicCallback
 * (see supervisor.h): rate-limited to ARCHIVER_RECONCILER_INTERVAL_SECONDS
 * by wall-clock time regardless of how often supervisor_loop() actually
 * invokes it, it re-lists this archiver's current memberships and adds
 * or removes supervised capture services to match -- the only place
 * outside archiver_reconciler_loop() itself that calls
 * supervisor_add_service()/supervisor_remove_service().
 */
static void
archiver_reconciler_tick(Supervisor *supervisor, void *context)
{
	Keeper *templateKeeper = (Keeper *) context;
	static time_t lastCheckedAt = 0;
	time_t now = time(NULL);

	if (lastCheckedAt != 0 && (now - lastCheckedAt) <
		ARCHIVER_RECONCILER_INTERVAL_SECONDS)
	{
		return;
	}

	lastCheckedAt = now;

	ArchiverMembershipArray memberships = { 0 };

	if (!monitor_list_archiver_memberships(&(templateKeeper->monitor),
										   templateKeeper->config.archiverId,
										   &memberships))
	{
		log_warn("Failed to list archiver memberships from the monitor, "
				 "will retry");
		return;
	}

	/* additions: a discovered membership with no matching supervised
	 * service yet */
	for (int i = 0; i < memberships.count; i++)
	{
		ArchiverMembership *membership = &(memberships.memberships[i]);
		Service *existing = NULL;

		if (find_membership_service(supervisor, membership->formation,
									membership->groupId, &existing))
		{
			continue;
		}

		Keeper *membershipKeeper = NULL;

		if (!build_membership_keeper(templateKeeper, membership, &membershipKeeper))
		{
			/* errors have already been logged; try again on the next tick */
			continue;
		}

		Service newService = {
			{ 0 }, RP_PERMANENT, -1,
			&service_archiver_capture_start,
			(void *) membershipKeeper, { 0 }
		};

		(void) membership_service_name(membership, newService.name,
									   sizeof(newService.name));

		log_info("Archiver reconciler: adding membership \"%s\"/%d",
				 membership->formation, membership->groupId);

		if (!supervisor_add_service(supervisor, newService))
		{
			log_warn("Failed to start capture for membership \"%s\"/%d, "
					 "will retry", membership->formation, membership->groupId);
			free(membershipKeeper);
		}
	}

	/*
	 * removals: a supervised capture service whose membership is no
	 * longer in the fresh discovery list -- collected first, then
	 * removed in a second pass, since supervisor_remove_service() packs
	 * the array and would otherwise invalidate this loop's own indices.
	 */
	Service *toRemove[ARCHIVER_MEMBERSHIP_ARRAY_MAX_COUNT] = { 0 };
	int toRemoveCount = 0;

	for (int i = 0; i < supervisor->serviceCount; i++)
	{
		Service *service = &(supervisor->services[i]);

		if (strncmp(service->name, ARCHIVER_CAPTURE_SERVICE_NAME_PREFIX,
					strlen(ARCHIVER_CAPTURE_SERVICE_NAME_PREFIX)) != 0)
		{
			continue;
		}

		Keeper *membershipKeeper = (Keeper *) service->context;
		bool stillMember = false;

		for (int j = 0; j < memberships.count; j++)
		{
			ArchiverMembership *membership = &(memberships.memberships[j]);

			if (streq(membershipKeeper->config.formation, membership->formation) &&
				membershipKeeper->config.groupId == membership->groupId)
			{
				stillMember = true;
				break;
			}
		}

		if (!stillMember && toRemoveCount < ARCHIVER_MEMBERSHIP_ARRAY_MAX_COUNT)
		{
			toRemove[toRemoveCount++] = service;
		}
	}

	for (int i = 0; i < toRemoveCount; i++)
	{
		Keeper *membershipKeeper = (Keeper *) toRemove[i]->context;
		pid_t pid = toRemove[i]->pid;
		char formation[NAMEDATALEN] = { 0 };
		int groupId = membershipKeeper->config.groupId;

		strlcpy(formation, membershipKeeper->config.formation, sizeof(formation));

		log_info("Archiver reconciler: removing membership \"%s\"/%d",
				 formation, groupId);

		if (supervisor_remove_service(supervisor, pid, SIGTERM))
		{
			free(membershipKeeper);
		}
		else
		{
			log_warn("Failed to remove capture for membership \"%s\"/%d, "
					 "will retry", formation, groupId);
		}
	}

	if (toRemoveCount > 0 || memberships.count != supervisor->serviceCount)
	{
		(void) archiver_reconciler_write_tracking_file(templateKeeper, supervisor);
	}
}


/*
 * service_archiver_reconciler_loop is the reconciler process's own body:
 * clean up after any previous instance of itself (see this file's own
 * header comment), discover this archiver's current memberships, start
 * one capture child per membership, persist the tracking file, then hand
 * off to supervisor_start_with_callback() with archiver_reconciler_tick()
 * as the periodic callback for everything from here on.
 */
static bool
service_archiver_reconciler_loop(Keeper *templateKeeper)
{
	(void) archiver_reconciler_cleanup_stale_children(templateKeeper);

	ArchiverMembershipArray memberships = { 0 };

	if (!monitor_list_archiver_memberships(&(templateKeeper->monitor),
										   templateKeeper->config.archiverId,
										   &memberships))
	{
		log_fatal("Failed to list archiver memberships from the monitor, "
				  "see above for details");
		return false;
	}

	int serviceCount = memberships.count;
	Service *services = (Service *) calloc(serviceCount > 0 ? serviceCount : 1,
										   sizeof(Service));

	if (services == NULL)
	{
		log_fatal("Failed to allocate memory for %d archiver memberships",
				  serviceCount);
		return false;
	}

	for (int i = 0; i < memberships.count; i++)
	{
		ArchiverMembership *membership = &(memberships.memberships[i]);
		Keeper *membershipKeeper = NULL;

		if (!build_membership_keeper(templateKeeper, membership, &membershipKeeper))
		{
			log_fatal("Failed to prepare archiver membership \"%s\"/%d, "
					  "see above for details",
					  membership->formation, membership->groupId);
			free(services);
			return false;
		}

		services[i].policy = RP_PERMANENT;
		services[i].pid = -1;
		services[i].startFunction = &service_archiver_capture_start;
		services[i].context = (void *) membershipKeeper;

		(void) membership_service_name(membership, services[i].name,
									   sizeof(services[i].name));
	}

	log_info("Archiver reconciler: starting capture for %d membership(s)",
			 memberships.count);

	char pidfile[MAXPGPATH] = { 0 };

	(void) archiver_reconciler_pidfile_path(templateKeeper, pidfile);

	return supervisor_start_with_callback(services, serviceCount, pidfile,
										  &archiver_reconciler_tick,
										  (void *) templateKeeper);
}


/*
 * service_archiver_reconciler_start forks the reconciler process itself
 * -- matching service_archiver_capture_start()/service_archiver_serve_
 * start_service()'s own fork-without-exec shape (service_archiver_run.c),
 * one level up: this is what start_archiver() supervises directly.
 */
bool
service_archiver_reconciler_start(void *context, pid_t *pid)
{
	Keeper *keeper = (Keeper *) context;

	fflush(stdout);
	fflush(stderr);

	pid_t fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the archiver reconciler process");
			return false;
		}

		case 0:
		{
			(void) set_signal_handlers(false);
			(void) set_ps_title("pg_autoctl: archiver reconciler");

			/* see service_archiver_capture_start()'s own comment on why
			 * each supervised child re-connects independently */
			if (!monitor_init(&(keeper->monitor), keeper->config.monitor_pguri))
			{
				log_fatal("Failed to contact the monitor, see above for details");
				exit(EXIT_CODE_MONITOR);
			}

			if (!service_archiver_reconciler_loop(keeper))
			{
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			exit(EXIT_CODE_QUIT);
		}

		default:
		{
			log_debug("pg_autoctl archiver reconciler process started in "
					  "subprocess %d", fpid);
			*pid = fpid;
			return true;
		}
	}
}
