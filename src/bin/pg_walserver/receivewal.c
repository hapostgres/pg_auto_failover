/*
 * src/bin/pg_walserver/receivewal.c
 *   See receivewal.h.
 *
 *   Design decision: fork() + execv() of *this same pg_walserver binary*,
 *   re-entering it as "pg_walserver internal service pg-receivewal --cluster
 *   <key> --upstream <conninfo> --path <dir>" (cli_internal.c), never a
 *   bare fork() with no exec() and never exec() of a separately-installed
 *   "pg_receivewal" binary. This mirrors pg_autoctl's own long-lived
 *   service pattern exactly (service_postgres_ctl_start(), cli_do_root.c's
 *   "pg_autoctl internal service postgres|listener|node-active"), not
 *   accept_loop.c's own per-*connection* fork/no-exec model -- a
 *   deliberately different lifecycle (see "Supervision shape" below).
 *   fork()+execv() of the running binary, rather than a plain fork(), is
 *   what makes a restarted receivewal worker safe to run as part of a container's
 *   PID 1: cli_internal.c's own header comment has the full rationale
 *   (live-upgrade safety, the same property pg_autoctl's own comment there
 *   documents for itself). pg_receivewal_main() itself (the vendored entry
 *   point, src/bin/common/vendor/pg_receivewal/) still runs in-process
 *   inside that fresh exec -- no third process, no real "pg_receivewal"
 *   binary needs to be installed/on $PATH anywhere.
 *
 *   Supervision shape: one long-lived child per active "receivewal = pull"
 *   cluster, not per-connection -- a different lifecycle from accept_loop.
 *   c's own per-connection children (reaped and forgotten the moment they
 *   exit): a receivewal worker child is alive for the server's whole lifetime,
 *   independent of any client connection, and needs restart-on-crash.
 *   Rather than a bespoke fork()/waitpid()/backoff loop, this reuses this
 *   project's own generic child-process supervisor, src/bin/common/
 *   process_supervisor.h -- a decoupled extraction of pg_autoctl's own
 *   supervisor.c's generic core (Service/RestartPolicy, the Erlang-
 *   inspired MaxR/MaxT restart-backoff ring buffer, and PID-1-safe orphan
 *   reaping via a single wildcard waitpid()) -- see that header's own
 *   comment for why this is an extraction, not a literal relocation of
 *   pg_autoctl/supervisor.c (which is too deeply entangled with the
 *   keeper/monitor/node-spec subsystem to move wholesale).
 *
 *   Reap-race hygiene: ws_receivewal_tick() below is the *only* place in this
 *   whole process allowed to call waitpid(-1, ...) (via process_
 *   supervisor_tick()'s own wildcard loop) -- accept_loop.c's own
 *   connection-child bookkeeping is threaded through as this function's
 *   otherChildExited callback instead of running its own, second wildcard
 *   wait. Two independent wildcard reapers in the same process is exactly
 *   the bug this project has already hit (see process_supervisor.h's own
 *   comment): whichever one's waitpid() call happens to run first
 *   silently consumes a zombie the other reaper needed to see, permanently
 *   hiding that child's death from it.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "receivewal.h"

#include "cli_upstream.h"
#include "env_utils.h"
#include "file_utils.h"
#include "log.h"
#include "pgctl.h"
#include "process_supervisor.h"
#include "clusters.h"
#include "string_utils.h"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))

/*
 * A cluster's own INI file is operator-written (or, later, written by
 * service_archiver_reconciler.c), not attacker input -- this bound exists
 * only to keep the receivewal-services array a fixed size, the same
 * WS_MAX_CONNECTIONS-style hardening accept_loop.c already applies to
 * connection children, for a completely different (but equally
 * operator-controlled) count.
 */
#define WS_RECEIVEWAL_MAX_CLUSTERS 64

/* how long ws_receivewal_stop_all() waits before escalating to SIGKILL */
#define WS_RECEIVEWAL_STOP_TIMEOUT_MS 5000

typedef struct WsReceivewalCluster
{
	char clusterKey[NAMEDATALEN + 16];
	char path[MAXPGPATH];
	char upstream[MAXCONNINFO];
	time_t startedAt;    /* set by start_one_receivewal_child(): the single
	                      * choke point every (re)start of this cluster's
	                      * receivewal worker goes through, initial start, reload-
	                      * driven restart, and tick-driven restart-on-
	                      * crash alike */
} WsReceivewalCluster;

static WsReceivewalCluster receivewalClusters[WS_RECEIVEWAL_MAX_CLUSTERS];
static ProcessService receivewalServices[WS_RECEIVEWAL_MAX_CLUSTERS];
static ProcessSupervisor receivewalSupervisor = { 0 };

extern char pg_autoctl_program[MAXPGPATH];     /* main.c, this binary's own
                                                * absolute path -- see
                                                * main.c's own comment */

static bool start_one_receivewal_child(void *context, pid_t *pid);


/*
 * ws_receivewal_start_all forks one supervised receivewal worker child per cluster in
 * clusters[0..clusterCount) with receivewalPull set (clusters.h), each running
 * this same pg_walserver binary re-exec'd into "internal service
 * pg-receivewal" (cli_internal.c), which runs the vendored pg_receivewal
 * against that cluster's own "upstream", writing straight into that
 * cluster's own "path". A cluster with receivewalPull but no "upstream" is
 * logged and skipped, not a startup failure -- so is a disabled cluster
 * (clusters.h's own WsCluster.disabled), silently: a dropped cluster never
 * gets its embedded receivewal worker started in the first place, the
 * same guarantee ws_receivewal_reload() already gives a cluster that
 * becomes disabled while already running. Called once, from
 * cli_serve_run(), after pg_walserver.ini/HBA validation succeeds and
 * before ws_accept_loop() starts. Must not be called more than once per
 * process.
 */
bool
ws_receivewal_start_all(const WsCluster *clusters, int clusterCount)
{
	int n = 0;

	for (int i = 0; i < clusterCount; i++)
	{
		if (!clusters[i].receivewalPull || clusters[i].disabled)
		{
			continue;
		}

		if (clusters[i].upstream[0] == '\0')
		{
			log_error("Cluster \"%s\" has \"receivewal = pull\" but no "
					  "\"upstream\" property: the embedded receivewal worker has "
					  "nowhere to pull WAL from -- not starting it for "
					  "this cluster", clusters[i].key);
			continue;
		}

		if (n >= WS_RECEIVEWAL_MAX_CLUSTERS)
		{
			log_error("Too many \"receivewal = pull\" clusters (max %d): not "
					  "starting an embedded receivewal worker for cluster \"%s\"",
					  WS_RECEIVEWAL_MAX_CLUSTERS, clusters[i].key);
			continue;
		}

		WsReceivewalCluster *cr = &receivewalClusters[n];

		memset(cr, 0, sizeof(WsReceivewalCluster));
		strlcpy(cr->clusterKey, clusters[i].key, sizeof(cr->clusterKey));
		strlcpy(cr->path, clusters[i].path, sizeof(cr->path));
		strlcpy(cr->upstream, clusters[i].upstream, sizeof(cr->upstream));

		ProcessService *service = &receivewalServices[n];

		memset(service, 0, sizeof(ProcessService));
		sformat(service->name, sizeof(service->name), "receivewal-%s",
				cr->clusterKey);
		service->policy = PROCESS_RP_PERMANENT;
		service->startFunction = start_one_receivewal_child;
		service->context = cr;

		n++;
	}

	process_supervisor_init(&receivewalSupervisor, receivewalServices, n);

	if (n == 0)
	{
		return true;
	}

	if (!process_supervisor_start_all(&receivewalSupervisor))
	{
		log_warn("Failed to start every embedded receivewal worker; the ones "
				 "that did start will still be supervised normally");
	}

	return true;
}


/*
 * ensure_receivewal_slot creates this cluster's own physical replication
 * slot on its upstream (clusters_slot_name() derives the name from the
 * cluster key, idempotently -- see pgctl_create_replication_slot()'s own
 * comment for why an already-existing slot is success, not an error). A
 * real, permanent slot -- not pg_basebackup's own temporary one -- is
 * what keeps the upstream from recycling a WAL segment this cluster's
 * receivewal worker hasn't fetched yet out from under it; see cli_
 * internal.c's own comment on cli_internal_pg_receivewal_run() for the
 * full rationale (a fresh cluster's very first connection racing the
 * upstream's own checkpoint can otherwise lose a segment permanently).
 * Best-effort only here: a failure is logged and start_one_receivewal_
 * child() still starts the worker regardless, the same "receivewal never
 * blocks itself on write-ahead-log-retention setup" trade-off this
 * project already makes for archive_command's own guarantee never being
 * a hard prerequisite for starting to stream.
 */
static void
ensure_receivewal_slot(const WsReceivewalCluster *cr)
{
	WsUpstreamTarget target = { 0 };

	if (!cli_parse_upstream_conninfo(cr->upstream, &target))
	{
		log_warn("Cluster \"%s\": failed to parse its own \"upstream\" to "
				 "create its replication slot -- starting the receivewal "
				 "worker without one, so a reconnect could lose a WAL "
				 "segment the upstream considers no longer needed",
				 cr->clusterKey);
		return;
	}

	char slotName[NAMEDATALEN] = { 0 };

	clusters_slot_name(cr->clusterKey, slotName, sizeof(slotName));

	ReplicationSource replicationSource = { 0 };

	replicationSource.primaryNode = target.node;
	strlcpy(replicationSource.userName, target.userName,
			sizeof(replicationSource.userName));
	strlcpy(replicationSource.applicationName, "pg_walserver_receivewal",
			sizeof(replicationSource.applicationName));
	replicationSource.sslOptions = target.sslOptions;

	if (env_exists("PGPASSWORD"))
	{
		(void) get_env_copy("PGPASSWORD", replicationSource.password,
							sizeof(replicationSource.password));
	}

	if (!pgctl_create_replication_slot(&replicationSource, slotName))
	{
		log_warn("Cluster \"%s\": failed to create replication slot \"%s\" "
				 "on its upstream -- starting the receivewal worker "
				 "without one, so a reconnect could lose a WAL segment "
				 "the upstream considers no longer needed",
				 cr->clusterKey, slotName);
	}
}


/*
 * start_one_receivewal_child forks and execv()s this same pg_walserver
 * binary as "internal service pg-receivewal --cluster ... --upstream ...
 * --path ..." -- see this file's own header comment for why fork()+
 * execv(), not a bare fork().
 */
static bool
start_one_receivewal_child(void *context, pid_t *pid)
{
	WsReceivewalCluster *cr = (WsReceivewalCluster *) context;

	ensure_receivewal_slot(cr);

	fflush(stdout);
	fflush(stderr);

	pid_t fpid = fork();

	if (fpid == -1)
	{
		log_error("Failed to fork the embedded receivewal worker for cluster "
				  "\"%s\": %m", cr->clusterKey);
		return false;
	}

	if (fpid == 0)
	{
		char *args[11];
		int argsIndex = 0;

		args[argsIndex++] = pg_autoctl_program;
		args[argsIndex++] = "internal";
		args[argsIndex++] = "service";
		args[argsIndex++] = "pg-receivewal";
		args[argsIndex++] = "--cluster";
		args[argsIndex++] = cr->clusterKey;
		args[argsIndex++] = "--upstream";
		args[argsIndex++] = cr->upstream;
		args[argsIndex++] = "--path";
		args[argsIndex++] = cr->path;
		args[argsIndex] = NULL;

		execv(pg_autoctl_program, args);

		/* only reached if execv() itself failed */
		log_fatal("execv(\"%s\"): %m", pg_autoctl_program);
		_exit(127);
	}

	*pid = fpid;
	cr->startedAt = time(NULL);

	log_info("Started the embedded receivewal worker for cluster \"%s\" (pid %d), "
			 "receiving into \"%s\"", cr->clusterKey, fpid, cr->path);

	return true;
}


/*
 * ws_receivewal_get_status fills out[0..min(serviceCount,maxOut)) with the
 * current status of every cluster ws_receivewal_start_all()/ws_receivewal_reload()
 * is tracking (whether or not each one is currently running), and returns
 * how many entries it filled. Used by accept_loop.c's own refresh_ps_
 * state() to keep the on-disk ps state file (ps_state.h) current.
 */
int
ws_receivewal_get_status(WsReceivewalStatus *out, int maxOut)
{
	int n = 0;

	for (int i = 0; i < receivewalSupervisor.serviceCount && n < maxOut; i++)
	{
		WsReceivewalCluster *cr = &receivewalClusters[i];
		ProcessService *service = &receivewalServices[i];
		WsReceivewalStatus *status = &out[n];

		memset(status, 0, sizeof(WsReceivewalStatus));
		strlcpy(status->clusterKey, cr->clusterKey, sizeof(status->clusterKey));
		strlcpy(status->path, cr->path, sizeof(status->path));
		strlcpy(status->upstream, cr->upstream, sizeof(status->upstream));
		status->pid = service->pid;
		status->startedAt = cr->startedAt;
		status->restarts = service->restartCounters.count > 0 ?
						   service->restartCounters.count - 1 : 0;

		n++;
	}

	return n;
}


/*
 * ws_receivewal_reload reconciles the running "receivewal = pull" receivewal worker set
 * against a freshly, successfully reloaded (SIGHUP) cluster list -- it never
 * restarts a receivewal worker whose cluster is unchanged:
 *
 *   - a cluster that newly has "receivewal = pull" (or is new outright) gets a
 *     receivewal worker started;
 *   - a cluster whose "receivewal = pull" was removed, whose cluster
 *     disappeared entirely, or that is now disabled ("cluster drop"
 *     without --purge, see clusters.h's own WsCluster.disabled comment),
 *     gets its receivewal worker stopped (SIGINT), and never gets a new
 *     one started for it either;
 *   - a cluster whose "upstream" or "path" changed while "receivewal = pull"
 *     stayed on gets stopped (SIGINT) and, once reaped, automatically
 *     restarted with the new values by the ordinary PERMANENT-policy
 *     restart path in ws_receivewal_tick() -- it cannot retarget an
 *     already-forked/exec'd pg_receivewal child in place, so this is
 *     always a stop-then-start, never a live retarget.
 *
 * Logs every start/stop/restart decision it makes. Must only be called
 * after ws_receivewal_start_all() has already run once.
 */
void
ws_receivewal_reload(const WsCluster *newClusters, int newClusterCount)
{
	bool *handled = (bool *) calloc(newClusterCount > 0 ? newClusterCount : 1,
									sizeof(bool));

	if (handled == NULL)
	{
		log_error("Reload: out of memory reconciling the embedded "
				  "receivewal worker set: leaving it as-is");
		return;
	}

	int started = 0, stopped = 0, restarted = 0, unchanged = 0;

	/* stop, or update-then-restart-in-place, every currently tracked
	 * receivewal worker whose cluster disappeared, lost "receivewal = pull", or changed
	 * "upstream"/"path" */
	for (int i = 0; i < receivewalSupervisor.serviceCount; i++)
	{
		ProcessService *service = &receivewalServices[i];
		WsReceivewalCluster *cr = &receivewalClusters[i];

		if (service->pid <= 0)
		{
			continue;   /* already stopped: a free slot for reuse below */
		}

		const WsCluster *want = NULL;
		int wantIndex = -1;

		for (int j = 0; j < newClusterCount; j++)
		{
			if (streq(newClusters[j].key, cr->clusterKey))
			{
				want = &newClusters[j];
				wantIndex = j;
				break;
			}
		}

		if (want == NULL || !want->receivewalPull || want->upstream[0] == '\0' ||
			want->disabled)
		{
			log_info("Reload: stopping the embedded receivewal worker for "
					 "cluster \"%s\" (pid %d): %s", cr->clusterKey, service->pid,
					 want != NULL && want->disabled
					 ? "cluster dropped (disabled)"
					 : "no longer \"receivewal = pull\"");
			service->policy = PROCESS_RP_TEMPORARY;
			(void) kill(service->pid, SIGINT);
			++stopped;
			continue;
		}

		handled[wantIndex] = true;

		if (!streq(cr->upstream, want->upstream) || !streq(cr->path, want->path))
		{
			log_info("Reload: restarting the embedded receivewal worker for "
					 "cluster \"%s\" (pid %d): \"upstream\"/\"path\" changed",
					 cr->clusterKey, service->pid);

			/*
			 * service->context already points at cr: updating it here means
			 * the ordinary PERMANENT-policy restart-on-exit path in
			 * ws_receivewal_tick() (process_supervisor_tick() underneath it)
			 * starts the next incarnation with the new upstream/path once
			 * this SIGINT is reaped -- it cannot retarget an already-forked/
			 * exec'd pg_receivewal child in place, so this is always a
			 * stop-then-start, never a live retarget.
			 */
			strlcpy(cr->upstream, want->upstream, sizeof(cr->upstream));
			strlcpy(cr->path, want->path, sizeof(cr->path));

			(void) kill(service->pid, SIGINT);
			++restarted;
		}
		else
		{
			++unchanged;
		}
	}

	/* start a receivewal worker for every newly-added (or newly "receivewal = pull")
	 * cluster not already handled above */
	for (int j = 0; j < newClusterCount; j++)
	{
		if (handled[j] || !newClusters[j].receivewalPull || newClusters[j].disabled)
		{
			continue;
		}

		if (newClusters[j].upstream[0] == '\0')
		{
			log_error("Reload: cluster \"%s\" has \"receivewal = pull\" but no "
					  "\"upstream\" property: not starting an embedded "
					  "receivewal worker for it", newClusters[j].key);
			continue;
		}

		int slot = -1;

		for (int i = 0; i < receivewalSupervisor.serviceCount; i++)
		{
			if (receivewalServices[i].pid <= 0)
			{
				slot = i;
				break;
			}
		}

		if (slot == -1)
		{
			if (receivewalSupervisor.serviceCount >= WS_RECEIVEWAL_MAX_CLUSTERS)
			{
				log_error("Reload: too many \"receivewal = pull\" clusters (max "
						  "%d): not starting an embedded receivewal worker for cluster "
						  "\"%s\"", WS_RECEIVEWAL_MAX_CLUSTERS, newClusters[j].key);
				continue;
			}

			slot = receivewalSupervisor.serviceCount++;
		}

		WsReceivewalCluster *cr = &receivewalClusters[slot];

		memset(cr, 0, sizeof(WsReceivewalCluster));
		strlcpy(cr->clusterKey, newClusters[j].key, sizeof(cr->clusterKey));
		strlcpy(cr->path, newClusters[j].path, sizeof(cr->path));
		strlcpy(cr->upstream, newClusters[j].upstream, sizeof(cr->upstream));

		ProcessService *service = &receivewalServices[slot];

		memset(service, 0, sizeof(ProcessService));
		sformat(service->name, sizeof(service->name), "receivewal-%s", cr->clusterKey);
		service->policy = PROCESS_RP_PERMANENT;
		service->startFunction = start_one_receivewal_child;
		service->context = cr;

		if (start_one_receivewal_child(cr, &service->pid))
		{
			process_restart_counters_start(&service->restartCounters,
										   (uint64_t) time(NULL));
			log_info("Reload: started a new embedded receivewal worker for "
					 "cluster \"%s\"", cr->clusterKey);
			++started;
		}
		else
		{
			log_error("Reload: failed to start an embedded receivewal worker "
					  "for cluster \"%s\"", cr->clusterKey);
			service->pid = -1;
		}
	}

	free(handled);

	log_info("Reload: receivewal worker reconciliation: %d started, %d stopped, "
			 "%d restarted, %d unchanged", started, stopped, restarted,
			 unchanged);
}


/*
 * ws_receivewal_tick drains every exited receivewal worker child via this process's
 * one and only wildcard waitpid(-1, WNOHANG) loop (process_supervisor.c),
 * restarting any that need it, and hands any pid it doesn't recognize
 * (as one of its own supervised receivewalWorkers) to otherChildExited -- the
 * caller's own separately tracked children (accept_loop.c's per-
 * connection children). Called once per ws_accept_loop() iteration,
 * *instead of* that loop running its own, second wildcard wait: see
 * process_supervisor.h's own comment for why two independent wildcard
 * reapers in the same process is a real, previously-hit bug, not a
 * theoretical concern. A no-op (beyond calling otherChildExited, if
 * given, for any of the caller's own exited children) when
 * ws_receivewal_start_all() was never called or started no children.
 */
void
ws_receivewal_tick(bool (*otherChildExited)(void *ctx, pid_t pid, int status),
				   void *otherCtx)
{
	process_supervisor_tick(&receivewalSupervisor, otherChildExited, otherCtx);
}


/*
 * ws_receivewal_stop_all signals every still-running receivewal worker child to stop
 * cleanly (SIGINT, matching pg_receivewal's own documented clean-stop
 * signal -- see receivewal.c's own comment), waits up to a bounded timeout
 * for all of them, and escalates to SIGKILL for anything still alive past
 * that. Called once, from ws_accept_loop(), right before the server
 * itself exits -- never leaves an orphaned receivewal worker child running past
 * pg_walserver's own shutdown.
 */
void
ws_receivewal_stop_all(void)
{
	/*
	 * SIGINT, not SIGTERM: cli_internal.c's own pgaf_install_stop_
	 * handlers() call (made right after execv(), inside the receivewal worker
	 * child itself) treats both identically as "stop cleanly", but SIGINT
	 * is what upstream pg_receivewal itself documents as its own
	 * clean-stop signal -- the same signal pg_autoctl's own
	 * service_archiver_pgreceivewal_ctl.c sends its own pg_receivewal
	 * child, for the same reason.
	 */
	process_supervisor_stop_all(&receivewalSupervisor, SIGINT,
								WS_RECEIVEWAL_STOP_TIMEOUT_MS);
}
