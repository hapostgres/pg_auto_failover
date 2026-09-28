/*
 * src/bin/pg_walserver/capture.c
 *   See capture.h.
 *
 *   Design decision: fork() + execv() of *this same pg_walserver binary*,
 *   re-entering it as "pg_walserver internal service pg-receivewal --route
 *   <key> --upstream <conninfo> --path <dir>" (cli_internal.c), never a
 *   bare fork() with no exec() and never exec() of a separately-installed
 *   "pg_receivewal" binary. This mirrors pg_autoctl's own long-lived
 *   service pattern exactly (service_postgres_ctl_start(), cli_do_root.c's
 *   "pg_autoctl internal service postgres|listener|node-active"), not
 *   accept_loop.c's own per-*connection* fork/no-exec model -- a
 *   deliberately different lifecycle (see "Supervision shape" below).
 *   fork()+execv() of the running binary, rather than a plain fork(), is
 *   what makes a restarted capturer safe to run as part of a container's
 *   PID 1: cli_internal.c's own header comment has the full rationale
 *   (live-upgrade safety, the same property pg_autoctl's own comment there
 *   documents for itself). pg_receivewal_main() itself (the vendored entry
 *   point, src/bin/common/vendor/pg_receivewal/) still runs in-process
 *   inside that fresh exec -- no third process, no real "pg_receivewal"
 *   binary needs to be installed/on $PATH anywhere.
 *
 *   Supervision shape: one long-lived child per active "capture = pull"
 *   route, not per-connection -- a different lifecycle from accept_loop.
 *   c's own per-connection children (reaped and forgotten the moment they
 *   exit): a capturer child is alive for the server's whole lifetime,
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
 *   Reap-race hygiene: ws_capture_tick() below is the *only* place in this
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

#include "capture.h"

#include "file_utils.h"
#include "log.h"
#include "process_supervisor.h"
#include "string_utils.h"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))

/*
 * A route's own INI file is operator-written (or, later, written by
 * service_archiver_reconciler.c), not attacker input -- this bound exists
 * only to keep the capture-services array a fixed size, the same
 * WS_MAX_CONNECTIONS-style hardening accept_loop.c already applies to
 * connection children, for a completely different (but equally
 * operator-controlled) count.
 */
#define WS_CAPTURE_MAX_ROUTES 64

/* how long ws_capture_stop_all() waits before escalating to SIGKILL */
#define WS_CAPTURE_STOP_TIMEOUT_MS 5000

typedef struct WsCaptureRoute
{
	char routeKey[NAMEDATALEN + 16];
	char path[MAXPGPATH];
	char upstream[MAXCONNINFO];
} WsCaptureRoute;

static WsCaptureRoute captureRoutes[WS_CAPTURE_MAX_ROUTES];
static ProcessService captureServices[WS_CAPTURE_MAX_ROUTES];
static ProcessSupervisor captureSupervisor = { 0 };

extern char pg_autoctl_program[MAXPGPATH];     /* main.c, this binary's own
                                                * absolute path -- see
                                                * main.c's own comment */

static bool start_one_capture_child(void *context, pid_t *pid);


/*
 * ws_capture_start_all -- see capture.h.
 */
bool
ws_capture_start_all(const WsRoute *routes, int routeCount)
{
	int n = 0;

	for (int i = 0; i < routeCount; i++)
	{
		if (!routes[i].capturePull)
		{
			continue;
		}

		if (routes[i].upstream[0] == '\0')
		{
			log_error("Route \"%s\" has \"capture = pull\" but no "
					  "\"upstream\" property: the embedded capturer has "
					  "nowhere to pull WAL from -- not starting it for "
					  "this route", routes[i].key);
			continue;
		}

		if (n >= WS_CAPTURE_MAX_ROUTES)
		{
			log_error("Too many \"capture = pull\" routes (max %d): not "
					  "starting an embedded capturer for route \"%s\"",
					  WS_CAPTURE_MAX_ROUTES, routes[i].key);
			continue;
		}

		WsCaptureRoute *cr = &captureRoutes[n];

		memset(cr, 0, sizeof(WsCaptureRoute));
		strlcpy(cr->routeKey, routes[i].key, sizeof(cr->routeKey));
		strlcpy(cr->path, routes[i].path, sizeof(cr->path));
		strlcpy(cr->upstream, routes[i].upstream, sizeof(cr->upstream));

		ProcessService *service = &captureServices[n];

		memset(service, 0, sizeof(ProcessService));
		sformat(service->name, sizeof(service->name), "capture-%s",
				cr->routeKey);
		service->policy = PROCESS_RP_PERMANENT;
		service->startFunction = start_one_capture_child;
		service->context = cr;

		n++;
	}

	process_supervisor_init(&captureSupervisor, captureServices, n);

	if (n == 0)
	{
		return true;
	}

	if (!process_supervisor_start_all(&captureSupervisor))
	{
		log_warn("Failed to start every embedded pull capturer; the ones "
				 "that did start will still be supervised normally");
	}

	return true;
}


/*
 * start_one_capture_child forks and execv()s this same pg_walserver
 * binary as "internal service pg-receivewal --route ... --upstream ...
 * --path ..." -- see this file's own header comment for why fork()+
 * execv(), not a bare fork().
 */
static bool
start_one_capture_child(void *context, pid_t *pid)
{
	WsCaptureRoute *cr = (WsCaptureRoute *) context;

	fflush(stdout);
	fflush(stderr);

	pid_t fpid = fork();

	if (fpid == -1)
	{
		log_error("Failed to fork the embedded pull capturer for route "
				  "\"%s\": %m", cr->routeKey);
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
		args[argsIndex++] = "--route";
		args[argsIndex++] = cr->routeKey;
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

	log_info("Started the embedded pull capturer for route \"%s\" (pid %d), "
			 "capturing into \"%s\"", cr->routeKey, fpid, cr->path);

	return true;
}


/*
 * ws_capture_reload -- see capture.h.
 */
void
ws_capture_reload(const WsRoute *newRoutes, int newRouteCount)
{
	bool *handled = (bool *) calloc(newRouteCount > 0 ? newRouteCount : 1,
									sizeof(bool));

	if (handled == NULL)
	{
		log_error("Reload: out of memory reconciling the embedded pull "
				  "capturer set: leaving it as-is");
		return;
	}

	int started = 0, stopped = 0, restarted = 0, unchanged = 0;

	/* stop, or update-then-restart-in-place, every currently tracked
	 * capturer whose route disappeared, lost "capture = pull", or changed
	 * "upstream"/"path" */
	for (int i = 0; i < captureSupervisor.serviceCount; i++)
	{
		ProcessService *service = &captureServices[i];
		WsCaptureRoute *cr = &captureRoutes[i];

		if (service->pid <= 0)
		{
			continue;   /* already stopped: a free slot for reuse below */
		}

		const WsRoute *want = NULL;
		int wantIndex = -1;

		for (int j = 0; j < newRouteCount; j++)
		{
			if (streq(newRoutes[j].key, cr->routeKey))
			{
				want = &newRoutes[j];
				wantIndex = j;
				break;
			}
		}

		if (want == NULL || !want->capturePull || want->upstream[0] == '\0')
		{
			log_info("Reload: stopping the embedded pull capturer for "
					 "route \"%s\" (pid %d): no longer \"capture = pull\"",
					 cr->routeKey, service->pid);
			service->policy = PROCESS_RP_TEMPORARY;
			(void) kill(service->pid, SIGINT);
			++stopped;
			continue;
		}

		handled[wantIndex] = true;

		if (!streq(cr->upstream, want->upstream) || !streq(cr->path, want->path))
		{
			log_info("Reload: restarting the embedded pull capturer for "
					 "route \"%s\" (pid %d): \"upstream\"/\"path\" changed",
					 cr->routeKey, service->pid);

			/*
			 * service->context already points at cr: updating it here means
			 * the ordinary PERMANENT-policy restart-on-exit path in
			 * ws_capture_tick() (process_supervisor_tick() underneath it)
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

	/* start a capturer for every newly-added (or newly "capture = pull")
	 * route not already handled above */
	for (int j = 0; j < newRouteCount; j++)
	{
		if (handled[j] || !newRoutes[j].capturePull)
		{
			continue;
		}

		if (newRoutes[j].upstream[0] == '\0')
		{
			log_error("Reload: route \"%s\" has \"capture = pull\" but no "
					  "\"upstream\" property: not starting an embedded "
					  "capturer for it", newRoutes[j].key);
			continue;
		}

		int slot = -1;

		for (int i = 0; i < captureSupervisor.serviceCount; i++)
		{
			if (captureServices[i].pid <= 0)
			{
				slot = i;
				break;
			}
		}

		if (slot == -1)
		{
			if (captureSupervisor.serviceCount >= WS_CAPTURE_MAX_ROUTES)
			{
				log_error("Reload: too many \"capture = pull\" routes (max "
						  "%d): not starting an embedded capturer for route "
						  "\"%s\"", WS_CAPTURE_MAX_ROUTES, newRoutes[j].key);
				continue;
			}

			slot = captureSupervisor.serviceCount++;
		}

		WsCaptureRoute *cr = &captureRoutes[slot];

		memset(cr, 0, sizeof(WsCaptureRoute));
		strlcpy(cr->routeKey, newRoutes[j].key, sizeof(cr->routeKey));
		strlcpy(cr->path, newRoutes[j].path, sizeof(cr->path));
		strlcpy(cr->upstream, newRoutes[j].upstream, sizeof(cr->upstream));

		ProcessService *service = &captureServices[slot];

		memset(service, 0, sizeof(ProcessService));
		sformat(service->name, sizeof(service->name), "capture-%s", cr->routeKey);
		service->policy = PROCESS_RP_PERMANENT;
		service->startFunction = start_one_capture_child;
		service->context = cr;

		if (start_one_capture_child(cr, &service->pid))
		{
			process_restart_counters_start(&service->restartCounters,
										   (uint64_t) time(NULL));
			log_info("Reload: started a new embedded pull capturer for "
					 "route \"%s\"", cr->routeKey);
			++started;
		}
		else
		{
			log_error("Reload: failed to start an embedded pull capturer "
					  "for route \"%s\"", cr->routeKey);
			service->pid = -1;
		}
	}

	free(handled);

	log_info("Reload: capturer reconciliation: %d started, %d stopped, "
			 "%d restarted, %d unchanged", started, stopped, restarted,
			 unchanged);
}


/*
 * ws_capture_tick -- see capture.h.
 */
void
ws_capture_tick(bool (*otherChildExited)(void *ctx, pid_t pid, int status),
				void *otherCtx)
{
	process_supervisor_tick(&captureSupervisor, otherChildExited, otherCtx);
}


/*
 * ws_capture_stop_all -- see capture.h.
 */
void
ws_capture_stop_all(void)
{
	/*
	 * SIGINT, not SIGTERM: cli_internal.c's own pgaf_install_stop_
	 * handlers() call (made right after execv(), inside the capturer
	 * child itself) treats both identically as "stop cleanly", but SIGINT
	 * is what upstream pg_receivewal itself documents as its own
	 * clean-stop signal -- the same signal pg_autoctl's own
	 * service_archiver_pgreceivewal_ctl.c sends its own pg_receivewal
	 * child, for the same reason.
	 */
	process_supervisor_stop_all(&captureSupervisor, SIGINT,
								WS_CAPTURE_STOP_TIMEOUT_MS);
}
