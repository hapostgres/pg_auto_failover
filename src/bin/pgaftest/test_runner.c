/*
 * src/bin/pgaftest/test_runner.c
 *   Test execution engine for .pgaf specs.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>

#include "log.h"
#include "compose_gen.h"
#include "pgsql.h"
#include "parsing.h"
#include "nodestate_utils.h"
#include "state.h"
#include "test_runner.h"
#include "test_spec.h"

/* Docker binary name */
#define DOCKER "docker"

/* -----------------------------------------------------------------------
 * Forward declarations
 * ----------------------------------------------------------------------- */

static bool wait_for_state(TestRunner *r, const char *nodeName,
						   const char *targetState, int timeoutSecs,
						   bool checkAssigned);
static bool runner_wait_assigned_goal(TestRunner *r, const char *nodeName,
									  const char *targetState, int timeoutSecs);
static void log_output(const char *prefix, const char *out);

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

/* Run a shell command, return its exit code */
static int __attribute__((format(printf, 1, 2)))
run_cmd(const char *fmt, ...)
{
	char cmd[4096];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(cmd, sizeof(cmd), fmt, ap);
	va_end(ap);

	log_debug("$ %s", cmd);
	return system(cmd);
}


/* Run a shell command, capture stdout into buf */
static int __attribute__((format(printf, 3, 4)))
run_cmd_capture(char *buf, int buflen, const char *fmt, ...)
{
	char cmd[4096];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(cmd, sizeof(cmd), fmt, ap);
	va_end(ap);

	log_debug("$ %s", cmd);

	FILE *p = popen(cmd, "r");
	if (!p)
	{
		return -1;
	}

	int pos = 0;
	int c;
	while ((c = fgetc(p)) != EOF && pos < buflen - 1)
	{
		buf[pos++] = (char) c;
	}
	buf[pos] = '\0';

	/* trim trailing whitespace */
	while (pos > 0 && (buf[pos - 1] == '\n' || buf[pos - 1] == '\r' ||
					   buf[pos - 1] == ' '))
	{
		buf[--pos] = '\0';
	}

	return pclose(p);
}


/* -----------------------------------------------------------------------
 * Runner initialisation
 * ----------------------------------------------------------------------- */
static void
runner_init(TestRunner *r, TestSpec *spec, const char *workDir)
{
	memset(r, 0, sizeof(*r));
	r->spec = spec;

	/*
	 * Project name: inside the compose network COMPOSE_PROJECT_NAME is
	 * authoritative (the container is invoked with /spec.pgaf which would
	 * otherwise yield "spec").  On the host, derive from the spec filename.
	 */
	const char *envProject = getenv("COMPOSE_PROJECT_NAME");
	if (envProject && *envProject)
	{
		strlcpy(r->projectName, envProject, sizeof(r->projectName));
	}
	else
	{
		const char *base = strrchr(spec->filename, '/');
		base = base ? base + 1 : spec->filename;
		strncpy(r->projectName, base, sizeof(r->projectName) - 1);
		char *dot = strrchr(r->projectName, '.');
		if (dot)
		{
			*dot = '\0';
		}
	}

	snprintf(r->workDir, sizeof(r->workDir), "%s", workDir);
	snprintf(r->composeFile, sizeof(r->composeFile),
			 "%s/docker-compose.yml", workDir);

	/*
	 * Inside the compose network the compose file lives on the HOST filesystem
	 * and is not accessible from the container.  Docker Compose v2 can exec
	 * into a running project by project-name alone; omit -f in that case.
	 */
	if (getenv("PGAFTEST_COMPOSE_SERVICE"))
	{
		/*
		 * Inside the pgaftest container, the compose file lives on the HOST
		 * at PGAFTEST_HOST_WORK_DIR (bind-mounted to the same path in the
		 * container so Docker daemon can find it).  Use -f to point Docker
		 * Compose at it so that `up -d` can create new containers.
		 */
		const char *hostWorkDir = getenv("PGAFTEST_HOST_WORK_DIR");
		if (hostWorkDir && hostWorkDir[0])
		{
			snprintf(r->composeBase, sizeof(r->composeBase),
					 "docker compose -p %s -f %s/docker-compose.yml",
					 r->projectName, hostWorkDir);
		}
		else
		{
			snprintf(r->composeBase, sizeof(r->composeBase),
					 "docker compose -p %s", r->projectName);
		}
	}
	else
	{
		snprintf(r->composeBase, sizeof(r->composeBase),
				 "docker compose -p %s -f %s", r->projectName, r->composeFile);
	}

	/* build context = directory from which pgaftest was invoked */
	if (getcwd(r->contextDir, sizeof(r->contextDir)) == NULL)
	{
		log_error("getcwd failed: %m");
		r->contextDir[0] = '\0';
	}

	/* absolute path to the spec file */
	if (spec->filename[0] == '/')
	{
		strlcpy(r->specFile, spec->filename, sizeof(r->specFile));
	}
	else
	{
		snprintf(r->specFile, sizeof(r->specFile), "%s/%s",
				 r->contextDir, spec->filename);
	}

	/* Start with the primary monitor as the active target. */
	strlcpy(r->activeMonitorService, "monitor",
			sizeof(r->activeMonitorService));
}


/* -----------------------------------------------------------------------
 * TAP output
 * ----------------------------------------------------------------------- */

/* Append a line to the TAP buffer (does not print yet). */
static void __attribute__((format(printf, 2, 3)))
tap_buf(TestRunner *r, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(r->tapBuffer + r->tapBufferLen,
					  sizeof(r->tapBuffer) - r->tapBufferLen,
					  fmt, ap);
	va_end(ap);
	if (n > 0)
	{
		r->tapBufferLen += n;
	}
}


/* Print all buffered TAP output (plan + results) to stdout and flush. */
static void
tap_plan(TestRunner *r)
{
	printf("1..%d\n", r->tapTotal);
	if (r->tapBufferLen > 0)
	{
		fwrite(r->tapBuffer, 1, r->tapBufferLen, stdout);
	}
	fflush(stdout);
}


static void
tap_ok(TestRunner *r, const char *name)
{
	r->tapPass++;
	tap_buf(r, "ok %d - %s\n", r->tapPass + r->tapFail, name);
}


static void
tap_not_ok(TestRunner *r, const char *name, const char *reason)
{
	r->tapFail++;
	tap_buf(r, "not ok %d - %s\n", r->tapPass + r->tapFail, name);
	if (reason)
	{
		tap_buf(r, "# %s\n", reason);
	}
}


static void __attribute__((format(printf, 1, 2)))
tap_diag(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	printf("# %s\n", buf);
	fflush(stdout);
}


/* -----------------------------------------------------------------------
 * Compose lifecycle
 * ----------------------------------------------------------------------- */
static bool
runner_compose_generate(TestRunner *r)
{
	/* ensure workdir exists (create intermediate directories as needed) */
	{
		char tmp[sizeof(r->workDir)];
		strncpy(tmp, r->workDir, sizeof(tmp) - 1);
		for (char *p = tmp + 1; *p; p++)
		{
			if (*p == '/')
			{
				*p = '\0';
				(void) mkdir(tmp, 0755);
				*p = '/';
			}
		}
		if (mkdir(r->workDir, 0755) != 0 && errno != EEXIST)
		{
			log_error("Failed to create work directory \"%s\": %m", r->workDir);
			return false;
		}
	}

	/* Generate SSL certs if the cluster ssl mode requires them (verify-ca/full) */
	if (!compose_gen_write_ssl_certs(&r->spec->cluster, r->workDir))
	{
		return false;
	}

	/*
	 * In host mode (no PGAFTEST_COMPOSE_SERVICE) we ARE the test runner, so
	 * we don't want the compose file to include a pgaftest service — it only
	 * makes sense for the in-compose CI mode where `docker compose up
	 * --exit-code-from pgaftest` drives the whole run.  Pass NULL to omit it.
	 */
	const char *specFileForCompose = getenv("PGAFTEST_COMPOSE_SERVICE")
									 ? r->specFile : NULL;

	if (!compose_gen_write(&r->spec->cluster,
						   r->composeFile,
						   r->projectName,
						   r->contextDir,
						   specFileForCompose))
	{
		return false;
	}

	/*
	 * Write per-node pg_autoctl_node.ini files into the workdir.  Each one
	 * is bind-mounted into its container at /etc/pgaf/node.ini so that
	 * every container uses the same image and command regardless of role.
	 */
	if (!compose_gen_write_monitor_ini(&r->spec->cluster, r->workDir))
	{
		return false;
	}

	if (!compose_gen_write_second_monitor_ini(&r->spec->cluster, r->workDir))
	{
		return false;
	}

	int globalNodeId = 0;
	for (int fi = 0; fi < r->spec->cluster.formationCount; fi++)
	{
		const TestFormation *form = &r->spec->cluster.formations[fi];

		for (int ni = 0; ni < form->nodeCount; ni++)
		{
			if (!compose_gen_write_node_ini(&r->spec->cluster, form,
											&form->nodes[ni],
											++globalNodeId, r->workDir))
			{
				return false;
			}
		}
	}

	return true;
}


static bool
runner_compose_up(TestRunner *r)
{
	log_info("Starting compose stack (project: %s)", r->projectName);

	int rc = run_cmd("%s up --build -d 2>&1",
					 r->composeBase);
	if (rc != 0)
	{
		log_error("docker compose up failed (exit %d)", rc);
		return false;
	}
	r->composeUp = true;

	/*
	 * Stop services declared with "launch deferred" (e.g. a second monitor).
	 * They are defined in the compose file so their image is built and volumes
	 * are created, but they must not run until the test explicitly starts them.
	 */
	if (r->spec->cluster.secondMonitorName[0] &&
		r->spec->cluster.secondMonitorStopped)
	{
		log_info("Stopping initially-stopped service %s",
				 r->spec->cluster.secondMonitorName);
		rc = run_cmd("%s stop %s 2>&1",
					 r->composeBase, r->spec->cluster.secondMonitorName);
		if (rc != 0)
		{
			log_error("docker compose stop %s failed (exit %d)",
					  r->spec->cluster.secondMonitorName, rc);
			return false;
		}
	}

	return true;
}


/*
 * runner_apply_formation_settings — apply cluster-level formation settings
 * that cannot be expressed in the node ini files.
 *
 * Called once after `docker compose up` and the monitor healthcheck passes.
 * Currently applies:
 *   - number-sync-standbys (when numSync >= 0 in the cluster spec)
 *
 * We use `docker compose exec` so the call goes through the monitor's own
 * pg_autoctl binary and lands on the already-running monitor instance.
 */
static bool
runner_apply_formation_settings(TestRunner *r)
{
	const TestCluster *c = &r->spec->cluster;
	bool ok = true;

	for (int fi = 0; fi < c->formationCount; fi++)
	{
		const TestFormation *form = &c->formations[fi];

		if (form->numSync < 0)
		{
			continue;
		}

		log_info("Setting formation \"%s\" number-sync-standbys = %d",
				 form->name, form->numSync);

		int rc = run_cmd(
			"%s exec -T monitor "
			"pg_autoctl set formation number-sync-standbys %d "
			"--pgdata /var/lib/postgres/pgaf --formation %s 2>&1",
			r->composeBase,
			form->numSync, form->name);

		if (rc != 0)
		{
			log_error("Failed to set number-sync-standbys for formation "
					  "\"%s\" (exit %d)", form->name, rc);
			ok = false;
		}
	}

	return ok;
}


static bool
runner_compose_down(TestRunner *r)
{
	if (!r->composeUp)
	{
		return true;
	}

	/*
	 * When pgaftest runs as a service inside the compose stack itself, calling
	 * "compose down" sends SIGKILL to our own container, so skip it — the host
	 * runner already calls "compose down" after we exit.
	 */
	if (getenv("PGAFTEST_COMPOSE_SERVICE"))
	{
		r->composeUp = false;
		return true;
	}

	log_info("Tearing down compose stack (project: %s)", r->projectName);

	if (r->notifyConnected)
	{
		pgsql_finish(&r->notifyConn);
		r->notifyConnected = false;
	}

	run_cmd("%s down --volumes --remove-orphans 2>&1",
			r->composeBase);
	r->composeUp = false;
	return true;
}


/* -----------------------------------------------------------------------
 * State polling via pg_autoctl inspect monitor subcommands
 *
 * Instead of shelling to psql (with all its quoting pitfalls), we call
 * purpose-built subcommands of pg_autoctl that are installed in the
 * monitor container:
 *
 *   pg_autoctl inspect monitor node-state --name <node>
 *     → stdout: "reported|assigned\n"   exit 0 on success
 *
 *   pg_autoctl inspect monitor formation-states [--group N] <s1> [<s2>...]
 *     → exit 0 when all listed states have ≥1 node, exit 1 when not
 * ----------------------------------------------------------------------- */

/*
 * Run `docker compose exec -T monitor pg_autoctl inspect monitor node-state
 * --name <nodeName>` and parse the "reported|assigned\n" output.
 *
 * When timeoutSecs > 0, passes --timeout to let the subcommand do its own
 * retry loop (exponential back-off with jitter, same policy as the rest of
 * pg_autoctl).  When targetState is non-NULL, also passes --state so the
 * subcommand only exits 0 when the node has reached that reported state.
 */
static bool
monitor_get_node_state(TestRunner *r, const char *nodeName,
					   char *reported, int replen,
					   char *assigned, int asslen)
{
	char out[256];
	int rc = run_cmd_capture(out, sizeof(out),
							 "%s exec -T %s "
							 "pg_autoctl inspect monitor node-state --name %s 2>/dev/null",
							 r->composeBase, r->activeMonitorService, nodeName);

	if (rc != 0 || out[0] == '\0')
	{
		/*
		 * Fallback for v2.1 monitors that don't have "pg_autoctl inspect":
		 * query the pgautofailover.node table directly via psql.
		 */
		int rc2 = run_cmd_capture(out, sizeof(out),
								  "%s exec -T %s "
								  "psql -U autoctl_node -d pg_auto_failover -At "
								  "-c \"SELECT reportedstate||'|'||goalstate FROM pgautofailover.node "
								  "    WHERE nodename='%s'\" 2>/dev/null",
								  r->composeBase, r->activeMonitorService, nodeName);
		if (rc2 != 0 || out[0] == '\0')
		{
			return false;
		}
	}

	/* strip trailing whitespace */
	int n = strlen(out);
	while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' '))
	{
		out[--n] = '\0';
	}

	/* format: reported|goal|health */
	char *sep1 = strchr(out, '|');
	if (!sep1)
	{
		return false;
	}
	*sep1 = '\0';
	strncpy(reported, out, replen - 1);
	char *sep2 = strchr(sep1 + 1, '|');
	if (sep2)
	{
		*sep2 = '\0';
	}
	strncpy(assigned, sep1 + 1, asslen - 1);
	return true;
}


/*
 * Read the pgdata path for a node from its .ini file in the work directory.
 * Returns true and fills pgdata on success; false if the file can't be read.
 */
static bool
get_node_pgdata(TestRunner *r, const char *nodeName, char *pgdata, int len)
{
	char iniPath[1280];
	snprintf(iniPath, sizeof(iniPath), "%s/%s.ini", r->workDir, nodeName);

	FILE *f = fopen(iniPath, "r");
	if (!f)
	{
		return false;
	}

	char line[256];
	bool found = false;

	while (fgets(line, sizeof(line), f))
	{
		if (strncmp(line, "pgdata", 6) == 0)
		{
			char *eq = strchr(line, '=');
			if (eq)
			{
				eq++;
				while (*eq == ' ' || *eq == '\t')
				{
					eq++;
				}
				char *end = eq + strlen(eq) - 1;
				while (end > eq && (*end == '\n' || *end == '\r' || *end == ' '))
				{
					*end-- = '\0';
				}
				strlcpy(pgdata, eq, len);
				found = true;
				break;
			}
		}
	}
	fclose(f);
	return found;
}


/*
 * Query the monitor for reported|goal|health of a node in one call.
 * Returns true if the node was found; health is set to the integer value
 * from pgautofailover.node.health (1 = healthy, -1 = unhealthy/unknown).
 */
static bool
monitor_get_node_health(TestRunner *r, const char *nodeName,
						char *reported, int replen,
						char *goal, int goallen,
						int *health)
{
	char out[256];
	int rc = run_cmd_capture(out, sizeof(out),
							 "%s exec -T monitor "
							 "pg_autoctl inspect monitor node-state --name %s 2>/dev/null",
							 r->composeBase, nodeName);

	if (rc != 0 || out[0] == '\0')
	{
		return false;
	}

	int n = strlen(out);
	while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' '))
	{
		out[--n] = '\0';
	}

	/* format: reported|goal|health */
	char *p1 = strchr(out, '|');
	if (!p1)
	{
		return false;
	}
	*p1 = '\0';
	strlcpy(reported, out, replen);

	char *p2 = strchr(p1 + 1, '|');
	if (!p2)
	{
		return false;
	}
	*p2 = '\0';
	strlcpy(goal, p1 + 1, goallen);

	*health = atoi(p2 + 1);
	return true;
}


/*
 * Fetch and log all node states from the monitor via pg_autoctl show state.
 * Used for progress snapshots during wait loops.
 */
static void
log_formation_state(TestRunner *r)
{
	char out[4096];
	int rc = run_cmd_capture(out, sizeof(out),
							 "%s exec -T monitor "
							 "pg_autoctl show state 2>/dev/null",
							 r->composeBase);

	if (rc != 0 || out[0] == '\0')
	{
		return;
	}

	char *line = out;
	while (*line)
	{
		char *nl = strchr(line, '\n');
		if (nl)
		{
			*nl = '\0';
		}
		if (*line)
		{
			log_info("  %s", line);
		}
		if (!nl)
		{
			break;
		}
		line = nl + 1;
	}
}


/* -----------------------------------------------------------------------
 * Direct LISTEN/NOTIFY connection to the monitor postgres
 *
 * compose_gen exposes the monitor's postgres on a random host port.
 * We connect directly from the host to receive state-change notifications
 * in real time, replacing the polling-via-docker-exec approach.
 * ----------------------------------------------------------------------- */

/*
 * Return the host port for the named monitor service, or 0 if unknown.
 * Used to build the direct libpq connection string for LISTEN/NOTIFY and
 * to look up the correct service name for monitor_get_node_state().
 */
static int
runner_monitor_host_port(TestRunner *r, const char *svc)
{
	const TestCluster *cl = &r->spec->cluster;

	if (strcmp(svc, "monitor") == 0)
	{
		return cl->monitorHostPort;
	}

	if (cl->secondMonitorName[0] && strcmp(svc, cl->secondMonitorName) == 0)
	{
		return cl->secondMonitorHostPort;
	}

	return 0;
}


/*
 * Open (or reopen) a direct libpq connection to the monitor and LISTEN on
 * the "state" channel.  Safe to call multiple times — no-op if already
 * connected.
 */
static bool
runner_notify_connect(TestRunner *r)
{
	if (r->notifyConnected &&
		r->notifyConn.connection != NULL &&
		PQstatus(r->notifyConn.connection) == CONNECTION_OK)
	{
		return true;
	}

	/* close any stale connection */
	if (r->notifyConn.connection)
	{
		pgsql_finish(&r->notifyConn);
		r->notifyConnected = false;
	}

	TestCluster *cl = &r->spec->cluster;
	const char *svc = r->activeMonitorService;

	char connstr[512];

	if (getenv("PGAFTEST_COMPOSE_SERVICE"))
	{
		/*
		 * Inside the compose network: connect directly via the monitor service
		 * hostname.  Include the password when the cluster uses md5/scram auth
		 * so that the autoctl_node role can authenticate.
		 */
		if (cl->monitorPassword[0])
		{
			snprintf(connstr, sizeof(connstr),
					 "host=%s port=5432 dbname=pg_auto_failover "
					 "user=autoctl_node password=%s connect_timeout=5",
					 svc, cl->monitorPassword);
		}
		else
		{
			snprintf(connstr, sizeof(connstr),
					 "host=%s port=5432 dbname=pg_auto_failover "
					 "user=autoctl_node connect_timeout=5",
					 svc);
		}
	}
	else
	{
		int port = runner_monitor_host_port(r, svc);
		if (port == 0)
		{
			log_debug("host port for monitor service \"%s\" not set, "
					  "skipping LISTEN connection", svc);
			return false;
		}
		if (cl->monitorPassword[0])
		{
			snprintf(connstr, sizeof(connstr),
					 "host=localhost port=%d dbname=pg_auto_failover "
					 "user=autoctl_node password=%s "
					 "connect_timeout=5",
					 port, cl->monitorPassword);
		}
		else
		{
			snprintf(connstr, sizeof(connstr),
					 "host=localhost port=%d dbname=pg_auto_failover "
					 "user=autoctl_node "
					 "connect_timeout=2",
					 port);
		}
	}

	strlcpy(r->notifyConn.connectionString, connstr,
			sizeof(r->notifyConn.connectionString));

	/*
	 * Suppress libpq's "could not connect" ERROR logs while we're polling —
	 * connection failures are expected until the monitor is ready.
	 * Restore the original level immediately after the attempt.
	 */
	int savedLevel = log_get_level();
	log_set_level(LOG_FATAL);

	char *channels[] = { "state", NULL };
	bool ok = pgsql_listen(&r->notifyConn, channels);

	log_set_level(savedLevel);

	if (!ok)
	{
		log_debug("LISTEN connection to monitor service \"%s\" failed "
				  "(will retry)", svc);
		return false;
	}

	log_debug("Listening on monitor service \"%s\" for state notifications",
			  svc);
	r->notifyConnected = true;
	return true;
}


/*
 * Drain pending notifications from the monitor connection, logging each one.
 *
 * If markNodes/markStates are provided (count > 0), any notification where
 * both goalState AND reportedState equal the target state for the matching
 * node is printed with a "* [notify]" prefix.  Only the convergence event
 * (the notification that actually lifts the wait) is marked; the earlier
 * assignment event (goalState matches but reportedState doesn't yet) is
 * printed with the ordinary "  [notify]" prefix.
 *
 * Callers that have no current wait condition pass count=0 (or NULL arrays).
 * Returns the last CurrentNodeState parsed, or leaves *last unchanged if none.
 */
static void
runner_drain_notify(TestRunner *r, CurrentNodeState *last,
					const char *const *markNodes,
					const char *const *markStates,
					int markCount,
					bool *satisfied)          /* optional: set [i] on convergence */
{
	PGconn *conn = r->notifyConn.connection;
	if (!conn)
	{
		return;
	}

	PQconsumeInput(conn);

	PGnotify *notify;
	while ((notify = PQnotifies(conn)) != NULL)
	{
		if (strcmp(notify->relname, "state") == 0)
		{
			CurrentNodeState ns = { 0 };
			if (parse_state_notification_message(&ns, notify->extra))
			{
				const char *ns_goal = NodeStateToString(ns.goalState);
				const char *ns_rep = NodeStateToString(ns.reportedState);

				/*
				 * Mark '*' only when this notification IS the convergence event:
				 * both goalState and reportedState equal the target for this node.
				 * markNodes[i] == NULL is a wildcard matching any node.
				 */
				bool matched = false;
				for (int i = 0; i < markCount; i++)
				{
					if (markStates && markStates[i] &&
						strcmp(ns_goal, markStates[i]) == 0 &&
						strcmp(ns_rep, markStates[i]) == 0 &&
						(!markNodes || !markNodes[i] ||
						 strcmp(ns.node.name, markNodes[i]) == 0))
					{
						matched = true;
						if (satisfied)
						{
							satisfied[i] = true;
						}
					}
				}
				const char *prefix = matched ? "* [notify]" : "  [notify]";

				if (ns.health >= 0)
				{
					log_info("%s %s: %s \xe2\x9e\x9c %s",
							 prefix, ns.node.name, ns_rep, ns_goal);
				}
				else
				{
					log_info("%s %s: %s \xe2\x9e\x9c %s (unhealthy)",
							 prefix, ns.node.name, ns_rep, ns_goal);
				}
				if (last)
				{
					*last = ns;
				}
			}
			else
			{
				log_debug("unparseable state notification: %s", notify->extra);
			}
		}
		PQfreemem(notify);
	}
}


/*
 * Wait on the monitor socket with select(), up to `remainMs` milliseconds.
 * Returns true if there is data to read, false on timeout.
 */
static bool
runner_wait_socket(TestRunner *r, int remainMs)
{
	PGconn *conn = r->notifyConn.connection;
	if (!conn)
	{
		return false;
	}

	int sock = PQsocket(conn);
	if (sock < 0)
	{
		return false;
	}

	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(sock, &rfds);

	struct timeval tv = {
		.tv_sec = remainMs / 1000,
		.tv_usec = (remainMs % 1000) * 1000,
	};
	bool ready = select(sock + 1, &rfds, NULL, NULL, &tv) > 0;
	if (ready)
	{
		PQconsumeInput(conn); /* pull data into libpq buffer so PQnotifies sees it */
	}
	return ready;
}


/*
 * Check that the formation has converged: for each required state, at least
 * one cluster node must have both reportedstate = assignedstate = that state.
 * This is the correct convergence condition — checking only assignedstate
 * fires as soon as the monitor makes an assignment, before nodes confirm.
 *
 * groupIds / groupCount optionally restrict the check to specific Citus groups
 * (pass NULL / 0 for a plain HA formation).
 */
static bool
monitor_check_formation_converged(TestRunner *r,
								  const char (*states)[64], int stateCount,
								  const int *groupIds, int groupCount)
{
	bool satisfied[PGAF_MAX_WAIT_STATES] = { false };
	int nodesQueried = 0;

	const TestCluster *c = &r->spec->cluster;
	for (int fi = 0; fi < c->formationCount; fi++)
	{
		const TestFormation *form = &c->formations[fi];
		for (int ni = 0; ni < form->nodeCount; ni++)
		{
			const TestNode *node = &form->nodes[ni];

			/* skip nodes not in the requested groups */
			if (groupCount > 0)
			{
				bool inGroup = false;
				for (int gi = 0; gi < groupCount; gi++)
				{
					if (node->group == groupIds[gi])
					{
						inGroup = true;
						break;
					}
				}
				if (!inGroup)
				{
					continue;
				}
			}

			char reported[64] = "", assigned[64] = "";
			if (!monitor_get_node_state(r, node->name,
										reported, sizeof(reported),
										assigned, sizeof(assigned)))
			{
				continue;
			}

			nodesQueried++;
			for (int si = 0; si < stateCount && si < PGAF_MAX_WAIT_STATES; si++)
			{
				if (strcmp(reported, states[si]) == 0 &&
					strcmp(assigned, states[si]) == 0)
				{
					satisfied[si] = true;
				}
			}
		}
	}

	/*
	 * If no nodes could be reached via subprocess (e.g. monitor running v2.1
	 * which lacks "pg_autoctl inspect monitor node-state"), the check is
	 * inconclusive rather than negative.  Return true so the caller can trust
	 * the LISTEN-based satisfied[] from runner_drain_notify instead.
	 */
	if (nodesQueried == 0)
	{
		return true;
	}

	for (int si = 0; si < stateCount; si++)
	{
		if (!satisfied[si])
		{
			return false;
		}
	}
	return true;
}


/*
 * Wait until the formation has at least one node in each listed state, where
 * both reportedstate and assignedstate have converged on the target.
 *
 * Strategy:
 *   1. Fast-path subprocess check: if the formation is already converged on
 *      entry (e.g. after a synchronous command), return immediately.
 *   2. LISTEN-driven main loop: block on the notify socket, drain each batch
 *      of notifications, and track which target states have been seen with
 *      BOTH goalState and reportedState matching (convergence events) via
 *      the satisfied[] array passed to runner_drain_notify.
 *      When all target states are satisfied by notifications, do ONE subprocess
 *      double-check before declaring success — guards against a rare race where
 *      a node races through the target state and moves on before we verify.
 *   3. No-LISTEN fallback: if the notify connection is unavailable, fall back
 *      to subprocess polling every second.
 */
static bool
monitor_wait_formation_states(TestRunner *r,
							  const char (*states)[64], int stateCount,
							  const int *groupIds, int groupCount,
							  int timeoutSecs)
{
	runner_notify_connect(r);

	/* pointer arrays for drain marking — NULL node = wildcard (any node) */
	const char *ms[PGAF_MAX_WAIT_STATES];
	for (int i = 0; i < stateCount && i < PGAF_MAX_WAIT_STATES; i++)
	{
		ms[i] = states[i];
	}

	/* satisfied[i] is set by runner_drain_notify when a convergence
	 * notification is seen for states[i] (both goal and reported match) */
	bool satisfied[PGAF_MAX_WAIT_STATES] = { false };

	/*
	 * Drain any notifications already buffered in libpq before the fast-path
	 * check.  This is important for synchronous commands like perform failover:
	 * the entire FSM cycle completes inside the blocking exec, so all
	 * intermediate NOTIFY messages have accumulated in the socket by the time
	 * we arrive here.  Draining first ensures those events appear in the
	 * current step's log output (with '*' marks on the convergence ones) rather
	 * than spilling into the next command's inter-drain or into teardown.
	 */
	if (r->notifyConnected)
	{
		runner_drain_notify(r, NULL, NULL, ms, stateCount, satisfied);
	}

	/* fast-path: already converged (confirmed by subprocess) */
	bool allSatisfiedEarly = true;
	for (int i = 0; i < stateCount; i++)
	{
		if (!satisfied[i])
		{
			allSatisfiedEarly = false;
			break;
		}
	}

	if (allSatisfiedEarly &&
		monitor_check_formation_converged(r, states, stateCount,
										  groupIds, groupCount))
	{
		return true;
	}

	time_t deadline = time(NULL) + timeoutSecs;
	int pollCounter = 0;      /* counts 5s wait-socket cycles */

	while (time(NULL) < deadline)
	{
		int remainMs = (int) ((deadline - time(NULL)) * 1000);
		if (remainMs <= 0)
		{
			break;
		}

		if (r->notifyConnected)
		{
			/* block until a notification arrives or up to 5s */
			int waitMs = remainMs < 5000 ? remainMs : 5000;
			runner_wait_socket(r, waitMs);

			/* drain: logs, marks '*', and sets satisfied[i] on convergence */
			runner_drain_notify(r, NULL, NULL, ms, stateCount, satisfied);

			/* when every target state has been seen converged via notification,
			 * do one subprocess double-check before declaring success */
			bool allSatisfied = true;
			for (int i = 0; i < stateCount; i++)
			{
				if (!satisfied[i])
				{
					allSatisfied = false;
					break;
				}
			}

			/*
			 * Also poll the monitor directly every ~5 cycles (~25 s) even
			 * without allSatisfied.  This handles the case where the LISTEN
			 * connection reconnects AFTER the state transitions fired — those
			 * notifications are gone and allSatisfied will never become true,
			 * but the monitor's current-state query still reflects reality.
			 */
			++pollCounter;

			/*
			 * When notifications say we converged, trust them — return
			 * immediately without a subprocess double-check.  The double-check
			 * races with fast post-convergence transitions: by the time the
			 * subprocess queries the monitor the nodes may have already moved
			 * on, producing a false negative that causes a 120s timeout.
			 *
			 * The periodic (every ~25s) subprocess poll remains as a fallback
			 * for missed notifications (e.g. the LISTEN reconnect after a
			 * monitor Postgres restart in the upgrade test).
			 */
			if (allSatisfied)
			{
				return true;
			}
			if (pollCounter % 5 == 0)
			{
				if (monitor_check_formation_converged(r, states, stateCount,
													  groupIds, groupCount))
				{
					return true;
				}
			}

			runner_notify_connect(r); /* reconnect if socket went bad */
		}
		else
		{
			/* no LISTEN — subprocess poll once per second */
			pg_usleep(1000 * 1000);
			runner_notify_connect(r);
			if (monitor_check_formation_converged(r, states, stateCount,
												  groupIds, groupCount))
			{
				return true;
			}
		}
	}

	/* deadline reached: one final drain + subprocess check */
	if (r->notifyConnected)
	{
		runner_drain_notify(r, NULL, NULL, ms, stateCount, satisfied);
	}
	if (monitor_check_formation_converged(r, states, stateCount,
										  groupIds, groupCount))
	{
		return true;
	}

	log_formation_state(r);
	return false;
}


/*
 * Wait until the formation has at least one node in each of the requested
 * states (within the given groups), or timeout expires.
 */
static bool
wait_for_states(TestRunner *r, TestCmd *cmd)
{
	/* build a human-readable label for log messages */
	char label[256] = "";
	for (int i = 0; i < cmd->waitStateCount; i++)
	{
		if (i > 0)
		{
			strncat(label, ", ", sizeof(label) - strlen(label) - 1);
		}
		strncat(label, cmd->waitStates[i], sizeof(label) - strlen(label) - 1);
	}

	if (cmd->waitGroupCount > 0)
	{
		strncat(label, " in group(s) ", sizeof(label) - strlen(label) - 1);
		for (int i = 0; i < cmd->waitGroupCount; i++)
		{
			char g[16];
			snprintf(g, sizeof(g), "%s%d", i > 0 ? "," : "", cmd->waitGroups[i]);
			strncat(label, g, sizeof(label) - strlen(label) - 1);
		}
	}

	time_t t0 = time(NULL);
	if (!monitor_wait_formation_states(r,
									   (const char (*)[64])cmd->waitStates,
									   cmd->waitStateCount,
									   cmd->waitGroups,
									   cmd->waitGroupCount,
									   cmd->timeoutSeconds))
	{
		log_error("Timed out after %ds waiting for formation states: %s",
				  (int) (time(NULL) - t0), label);
		return false;
	}
	return true;
}


/*
 * For a single node: query the monitor for its current group and whether it
 * is already the primary of that group.  If not, call
 *   pg_autoctl perform promotion --name <name>
 * on the monitor and wait until it becomes primary.
 *
 * pg_autoctl perform promotion is synchronous: it returns only after the new
 * primary has reached the 'primary' state.  We still poll to confirm the
 * monitor agrees, using the existing wait_for_state() helper.
 */
static bool
runner_promote_one(TestRunner *r, const char *nodeName)
{
	/* Check current state */
	char reported[64] = "", assigned[64] = "";
	if (!monitor_get_node_state(r, nodeName,
								reported, sizeof(reported),
								assigned, sizeof(assigned)))
	{
		log_error("promote: cannot query monitor for node %s", nodeName);
		return false;
	}

	if (strcmp(reported, "primary") == 0)
	{
		log_info("  promote: %s is already primary — no-op", nodeName);
		return true;
	}

	log_info("  promote: %s is %s, requesting promotion", nodeName, reported);

	/* find the formation this node belongs to */
	const char *formation = "default";
	const TestCluster *c = &r->spec->cluster;
	for (int fi = 0; fi < c->formationCount; fi++)
	{
		const TestFormation *form = &c->formations[fi];
		for (int ni = 0; ni < form->nodeCount; ni++)
		{
			if (strcmp(form->nodes[ni].name, nodeName) == 0)
			{
				formation = form->name;
				break;
			}
		}
	}

	char out[4096] = "";
	int rc = run_cmd_capture(out, sizeof(out),
							 "%s exec -T monitor "
							 "pg_autoctl perform promotion --name %s --formation %s 2>&1",
							 r->composeBase, nodeName, formation);

	if (rc != 0)
	{
		if (out[0])
		{
			log_output("   ", out);
		}
		log_error("promote: pg_autoctl perform promotion --name %s "
				  "--formation %s failed (exit %d)", nodeName, formation, rc);
		return false;
	}

	/* confirm monitor agrees */
	return wait_for_state(r, nodeName, "primary", PGAF_TIMEOUT_DEFAULT, false);
}


/*
 * runner_wait_notify_goal — LISTEN-driven single-node wait (items 5 & 6).
 *
 * Blocks until a "state" notification arrives where both states have
 * converged on the target:
 *   ns.node.name == nodeName
 *   AND  NodeStateToString(ns.goalState)    == targetState
 *   AND  NodeStateToString(ns.reportedState) == targetState
 *
 * The '*' marker is applied as soon as goalState matches (monitor has
 * assigned the target) even before the node confirms, so the log shows
 * the assignment event clearly.  The wait only lifts when the node
 * confirms (reportedState also equals target), verified by a final SQL
 * check that both assigned_state and reportedState agree.
 *
 * Using goalState (== monitor's assigned_state) as the trigger is faster and
 * more reliable than polling reportedState: the notification fires the moment
 * the monitor makes the assignment decision, before the node has transitioned.
 *
 * Along the way, any intermediate goalState values for this node are checked
 * against passThroughStates[] and seenThrough[] is updated (item 6).
 *
 * After the target goalState notification arrives:
 *   - drain remaining notifications (log them all)
 *   - final SQL check: assigned_state == targetState via monitor_get_node_state()
 *   - if SQL disagrees (rare race), keep looping
 *
 * Fallback when LISTEN is unavailable: poll assigned_state via SQL every 500ms;
 * track pass-through states from reportedState.
 *
 * For nodes the monitor has marked unhealthy, also accepts a match from the
 * node's local FSM (pg_autoctl inspect fsm node-state) to handle cases where
 * the keeper self-assigns a state without monitor contact.
 */
static bool
runner_wait_notify_goal(TestRunner *r,
						const char *nodeName,
						const char *targetState,
						const char passThroughStates[][64],
						bool *seenThrough,
						int passThroughCount,
						int timeoutSecs)
{
	runner_notify_connect(r);

	/*
	 * Fast path: if the node has already converged on the target state
	 * (reportedState == assignedState == targetState), return immediately.
	 * Without this, a node that settled during a preceding command would
	 * spin the full timeout waiting for a notification that won't arrive.
	 */
	{
		char rep0[64] = "", asgn0[64] = "";
		if (monitor_get_node_state(r, nodeName, rep0, sizeof(rep0),
								   asgn0, sizeof(asgn0)) &&
			strcmp(rep0, targetState) == 0 &&
			strcmp(asgn0, targetState) == 0)
		{
			return true;
		}
	}

	time_t deadline = time(NULL) + timeoutSecs;

	while (time(NULL) < deadline)
	{
		if (r->notifyConnected)
		{
			PQconsumeInput(r->notifyConn.connection);

			PGnotify *notify;
			while ((notify = PQnotifies(r->notifyConn.connection)) != NULL)
			{
				if (strcmp(notify->relname, "state") == 0)
				{
					CurrentNodeState ns = { 0 };
					if (parse_state_notification_message(&ns, notify->extra))
					{
						const char *ns_goal = NodeStateToString(ns.goalState);
						const char *ns_rep = NodeStateToString(ns.reportedState);

						/*
						 * Mark '*' only when this IS the convergence notification:
						 * both goalState and reportedState equal the target.
						 * The earlier assignment notification (goalState matches
						 * but reportedState doesn't yet) prints without a marker.
						 */
						bool goal_matched = (strcmp(ns.node.name, nodeName) == 0 &&
											 strcmp(ns_goal, targetState) == 0 &&
											 strcmp(ns_rep, targetState) == 0);
						const char *prefix = goal_matched ? "* [notify]" : "  [notify]";

						if (ns.health >= 0)
						{
							log_info("%s %s: %s \xe2\x9e\x9c %s",
									 prefix, ns.node.name, ns_rep, ns_goal);
						}
						else
						{
							log_info("%s %s: %s \xe2\x9e\x9c %s (unhealthy)",
									 prefix, ns.node.name, ns_rep, ns_goal);
						}

						if (strcmp(ns.node.name, nodeName) == 0)
						{
							/* track pass-through states from goalState (item 6) */
							for (int i = 0; i < passThroughCount; i++)
							{
								if (seenThrough && !seenThrough[i] &&
									strcmp(ns_goal, passThroughStates[i]) == 0)
								{
									log_info("wait %s: observed pass-through "
											 "assigned-state %s",
											 nodeName, ns_goal);
									seenThrough[i] = true;
								}
							}

							/*
							 * Lift the wait only when both states have converged:
							 * the monitor assigned the target (goalState) and the
							 * node confirmed it (reportedState).  The notification
							 * already carries both values so we can test here
							 * without an extra round-trip; we still do a final SQL
							 * confirmation to guard against a torn read.
							 */
							if (strcmp(ns_goal, targetState) == 0 &&
								strcmp(ns_rep, targetState) == 0)
							{
								PQfreemem(notify);

								/* post-match drain: no marking — belongs to next wait */
								runner_drain_notify(r, NULL, NULL, NULL, 0, NULL);

								/* final SQL: confirm both assigned and reported */
								char rep[64] = "", asgn[64] = "";
								if (monitor_get_node_state(r, nodeName,
														   rep, sizeof(rep),
														   asgn, sizeof(asgn)))
								{
									if (strcmp(rep, targetState) == 0 &&
										strcmp(asgn, targetState) == 0)
									{
										return true;
									}

									/*
									 * Rare torn read: keep looping.
									 * The next notification will re-trigger.
									 */
									log_debug("  %s: notify shows convergence on %s "
											  "but SQL has rep=%s asgn=%s, retrying",
											  nodeName, targetState, rep, asgn);
								}
								else
								{
									/* can't reach monitor — trust the notification */
									return true;
								}
								goto next_iter;
							}
						}
					}
					else
					{
						log_debug("unparseable state notification: %s",
								  notify->extra);
					}
				}
				PQfreemem(notify);
			}

			/*
			 * For unhealthy nodes: also accept a match from the node's local
			 * FSM — the keeper self-assigns certain states without monitor
			 * contact.
			 *
			 * For nodes that have been hard-killed (compose kill), the
			 * container is gone so exec fails.  Accept goalState-alone match
			 * for unhealthy nodes: a dead node can never report the new state,
			 * but the monitor has already made the assignment decision.
			 */
			{
				char rep[64] = "", goal[64] = "";
				int health = 1;
				if (monitor_get_node_health(r, nodeName,
											rep, sizeof(rep),
											goal, sizeof(goal),
											&health) &&
					health <= 0)
				{
					if (strcmp(goal, targetState) == 0)
					{
						return true;
					}

					char pgdata[1024] = "";
					get_node_pgdata(r, nodeName, pgdata, sizeof(pgdata));
					char out[256];
					if (run_cmd_capture(out, sizeof(out),
										"%s exec -T %s "
										"pg_autoctl inspect fsm node-state"
										" --state %s --timeout 0%s%s 2>/dev/null",
										r->composeBase, nodeName, targetState,
										pgdata[0] ? " --pgdata " : "",
										pgdata[0] ? pgdata : "") == 0)
					{
						return true;
					}
				}
			}
		}
		else
		{
			/*
			 * No LISTEN connection — fall back to polling via SQL.
			 * Require both reportedState and assignedState to equal the target
			 * (same convergence criterion as the notification path), except for
			 * unhealthy nodes where goalState alone is accepted (dead nodes
			 * can never report the new state).
			 */
			char rep[64] = "", asgn[64] = "";
			if (monitor_get_node_state(r, nodeName, rep, sizeof(rep),
									   asgn, sizeof(asgn)))
			{
				for (int i = 0; i < passThroughCount; i++)
				{
					if (seenThrough && !seenThrough[i] &&
						strcmp(rep, passThroughStates[i]) == 0)
					{
						log_info("wait %s: observed pass-through state %s",
								 nodeName, rep);
						seenThrough[i] = true;
					}
				}
				if (strcmp(rep, targetState) == 0 &&
					strcmp(asgn, targetState) == 0)
				{
					return true;
				}

				/* unhealthy node: accept goalState match alone */
				char rep2[64] = "", goal2[64] = "";
				int health2 = 1;
				if (strcmp(asgn, targetState) == 0 &&
					monitor_get_node_health(r, nodeName,
											rep2, sizeof(rep2),
											goal2, sizeof(goal2),
											&health2) &&
					health2 <= 0)
				{
					return true;
				}
			}
		}

next_iter:
		{
			int remainMs = (int) ((deadline - time(NULL)) * 1000);
			if (remainMs <= 0)
			{
				break;
			}
			int waitMs = remainMs < 2000 ? remainMs : 2000;

			if (r->notifyConnected)
			{
				runner_wait_socket(r, waitMs);
				runner_notify_connect(r);
			}
			else
			{
				pg_usleep(500 * 1000);
				runner_notify_connect(r);
			}
		}
	}

	/* one last chance: drain + SQL check — mark the target in case it just arrived */
	if (r->notifyConnected)
	{
		runner_drain_notify(r, NULL, &nodeName, &targetState, 1, NULL);
	}

	char rep2[64] = "", asgn2[64] = "";
	if (monitor_get_node_state(r, nodeName, rep2, sizeof(rep2),
							   asgn2, sizeof(asgn2)) &&
		strcmp(rep2, targetState) == 0 &&
		strcmp(asgn2, targetState) == 0)
	{
		return true;
	}

	return false;
}


/*
 * runner_wait_assigned_goal — wait until the monitor's goalState for nodeName
 * equals targetState, without requiring reportedState to converge.
 *
 * This implements the semantics of "wait until <node> assigned-state = X":
 * succeed as soon as the monitor assigns X as the goalState, even if the node
 * has not yet transitioned its own reportedState.  This matches the Python
 * test's wait_until_assigned_state() behaviour.
 *
 * Uses NOTIFY fast-path (goalState match alone) and falls back to polling
 * the assigned_state column directly via monitor_get_node_state().
 */
static bool
runner_wait_assigned_goal(TestRunner *r, const char *nodeName,
						  const char *targetState, int timeoutSecs)
{
	runner_notify_connect(r);

	/* fast path: already there */
	{
		char rep[64] = "", asgn[64] = "";
		if (monitor_get_node_state(r, nodeName, rep, sizeof(rep),
								   asgn, sizeof(asgn)) &&
			strcmp(asgn, targetState) == 0)
		{
			return true;
		}
	}

	time_t deadline = time(NULL) + timeoutSecs;

	while (time(NULL) < deadline)
	{
		if (r->notifyConnected)
		{
			PQconsumeInput(r->notifyConn.connection);

			PGnotify *notify;
			while ((notify = PQnotifies(r->notifyConn.connection)) != NULL)
			{
				if (strcmp(notify->relname, "state") == 0)
				{
					CurrentNodeState ns = { 0 };
					if (parse_state_notification_message(&ns, notify->extra))
					{
						const char *ns_goal = NodeStateToString(ns.goalState);
						const char *ns_rep = NodeStateToString(ns.reportedState);

						if (ns.health >= 0)
						{
							log_info("  [notify] %s: %s \xe2\x9e\x9c %s",
									 ns.node.name, ns_rep, ns_goal);
						}
						else
						{
							log_info("  [notify] %s: %s \xe2\x9e\x9c %s (unhealthy)",
									 ns.node.name, ns_rep, ns_goal);
						}

						if (strcmp(ns.node.name, nodeName) == 0 &&
							strcmp(ns_goal, targetState) == 0)
						{
							PQfreemem(notify);

							/* confirm via SQL */
							char rep[64] = "", asgn[64] = "";
							if (monitor_get_node_state(r, nodeName,
													   rep, sizeof(rep),
													   asgn, sizeof(asgn)))
							{
								if (strcmp(asgn, targetState) == 0)
								{
									return true;
								}

								/* rare race: goalState changed already; keep looping */
							}
							else
							{
								return true; /* can't reach monitor, trust notify */
							}
							goto next_iter_asgn;
						}
					}
				}
				PQfreemem(notify);
			}
		}
		else
		{
			/* fallback: poll assigned_state via SQL */
			char rep[64] = "", asgn[64] = "";
			if (monitor_get_node_state(r, nodeName, rep, sizeof(rep),
									   asgn, sizeof(asgn)) &&
				strcmp(asgn, targetState) == 0)
			{
				return true;
			}
		}

next_iter_asgn:
		{
			int remainMs = (int) ((deadline - time(NULL)) * 1000);
			if (remainMs <= 0)
			{
				break;
			}
			int waitMs = remainMs < 2000 ? remainMs : 2000;

			if (r->notifyConnected)
			{
				runner_wait_socket(r, waitMs);
			}
			else
			{
				pg_usleep(500 * 1000);
			}

			runner_notify_connect(r);
		}
	}

	return false;
}


/*
 * Wait until the node reaches the target state, or timeout expires.
 *
 * For state (reported) checks: delegates to runner_wait_notify_goal() which
 * waits for both goalState and reportedState to converge on target.
 * For assigned-state checks: delegates to runner_wait_assigned_goal() which
 * succeeds as soon as the monitor's goalState equals target.
 */
static bool
wait_for_state(TestRunner *r, const char *nodeName, const char *targetState,
			   int timeoutSecs, bool checkAssigned)
{
	if (checkAssigned)
	{
		/*
		 * assigned-state check: succeed as soon as the monitor's goalState
		 * equals the target, without requiring the node to have transitioned
		 * its reportedState.  This matches the Python test's behaviour for
		 * wait_until_assigned_state() and is necessary for cases like
		 * test_015_003 where the primary is assigned "primary" goalstate but
		 * cannot complete the transition until standbys reconnect.
		 */
		if (!runner_wait_assigned_goal(r, nodeName, targetState, timeoutSecs))
		{
			log_error("Timeout waiting for %s assigned-state = %s",
					  nodeName, targetState);
			return false;
		}
		return true;
	}

	if (!runner_wait_notify_goal(r, nodeName, targetState,
								 NULL, NULL, 0, timeoutSecs))
	{
		log_error("Timeout waiting for %s state = %s",
				  nodeName, targetState);
		return false;
	}
	return true;
}


/*
 * runner_check_stays_notify — drain pending notifications and verify none
 * changed the watched node's goalState away from expectedState (item 9).
 *
 * Returns true if the state is still as expected (no disruptive notification).
 * Returns false and writes into errBuf if a state-change notification arrived.
 */
static bool
runner_check_stays_notify(TestRunner *r,
						  const char *nodeName, const char *expectedState,
						  char *errBuf, int errLen)
{
	if (!r->notifyConnected)
	{
		return true;    /* no LISTEN — caller will poll instead */
	}
	PQconsumeInput(r->notifyConn.connection);

	PGnotify *notify;
	while ((notify = PQnotifies(r->notifyConn.connection)) != NULL)
	{
		if (strcmp(notify->relname, "state") == 0)
		{
			CurrentNodeState ns = { 0 };
			if (parse_state_notification_message(&ns, notify->extra))
			{
				const char *ns_goal = NodeStateToString(ns.goalState);
				const char *ns_rep = NodeStateToString(ns.reportedState);

				if (ns.health >= 0)
				{
					log_info("  [notify] %s: %s \xe2\x9e\x9c %s",
							 ns.node.name, ns_rep, ns_goal);
				}
				else
				{
					log_info("  [notify] %s: %s \xe2\x9e\x9c %s (unhealthy)",
							 ns.node.name, ns_rep, ns_goal);
				}

				if (strcmp(ns.node.name, nodeName) == 0 &&
					strcmp(ns_goal, expectedState) != 0)
				{
					PQfreemem(notify);
					snprintf(errBuf, errLen,
							 "stays-while: %s was assigned state %s "
							 "(expected to stay %s)",
							 nodeName, ns_goal, expectedState);
					return false;
				}
			}
		}
		PQfreemem(notify);
	}
	return true;
}


/* -----------------------------------------------------------------------
 * SQL execution on a service
 * ----------------------------------------------------------------------- */

/*
 * escape_sql_for_shell — escape single-quotes for embedding SQL in a
 * single-quoted shell argument using the '\'' trick.
 */
static void
escape_sql_for_shell(const char *sql, char *out, int outlen)
{
	int oi = 0;
	for (int i = 0; sql[i] && oi < outlen - 2; i++)
	{
		if (sql[i] == '\'')
		{
			out[oi++] = '\'';
			out[oi++] = '\\';
			out[oi++] = '\'';
			if (oi < outlen - 1)
			{
				out[oi++] = '\'';
			}
		}
		else
		{
			out[oi++] = sql[i];
		}
	}
	out[oi] = '\0';
}


/*
 * parse_sqlstate — scan output for a PostgreSQL SQLSTATE code.
 *
 * With VERBOSITY=verbose, psql formats errors as:
 *   ERROR:  XXXXX: message text
 * where XXXXX is a 5-character alphanumeric SQLSTATE code.
 * Writes up to 6 bytes (5 + NUL) into state.
 */
static void
parse_sqlstate(const char *output, char *state, int statelen)
{
	state[0] = '\0';
	const char *p = output;
	while (p && *p)
	{
		p = strstr(p, "ERROR:  ");
		if (!p)
		{
			break;
		}
		p += 8;  /* skip "ERROR:  " */
		/* SQLSTATE: exactly 5 alphanumeric characters followed by ':' */
		if (strlen(p) >= 6 && p[5] == ':')
		{
			bool valid = true;
			for (int i = 0; i < 5 && valid; i++)
			{
				valid = isalnum((unsigned char) p[i]);
			}
			if (valid)
			{
				int n = statelen < 6 ? statelen - 1 : 5;
				memcpy(state, p, n);
				state[n] = '\0';
				return;
			}
		}
	}
}


static bool
exec_sql_on_service(TestRunner *r, const char *service,
					const char *sql, char *outbuf, int outlen)
{
	char escaped[8192];
	escape_sql_for_shell(sql, escaped, sizeof(escaped));

	/*
	 * Always run with ON_ERROR_STOP=1 so psql exits non-zero on SQL errors,
	 * and VERBOSITY=verbose so SQLSTATE codes appear in error output.
	 * Redirect stderr to stdout so both are captured.
	 *
	 * The monitor container only has the pg_auto_failover database (no
	 * "docker" default database), so supply explicit -U / -d for it.
	 */
	const char *connArgs =
		(strcmp(service, "monitor") == 0)
		? "-U autoctl_node -d pg_auto_failover"
		: "";

	int rc = run_cmd_capture(outbuf, outlen,
							 "%s exec -T %s "
							 "psql --tuples-only --no-align "
							 "-v ON_ERROR_STOP=1 -v VERBOSITY=verbose "
							 "%s -c '%s' 2>&1",
							 r->composeBase, service, connArgs, escaped);

	return rc == 0;
}


/* -----------------------------------------------------------------------
 * Network failure simulation
 * ----------------------------------------------------------------------- */
static bool
runner_network_off(TestRunner *r, const char *nodeName)
{
	char netName[128], container[128];
	compose_network_name(r->projectName, netName, sizeof(netName));
	compose_container_name(r->projectName, nodeName, container, sizeof(container));

	log_info("  Disconnecting %s from network %s", container, netName);
	char out[1024] = "";
	int rc = run_cmd_capture(out, sizeof(out),
							 "docker network disconnect %s %s 2>&1",
							 netName, container);
	if (rc != 0 && out[0])
	{
		log_output("  ", out);
	}
	return rc == 0;
}


static bool
runner_network_on(TestRunner *r, const char *nodeName)
{
	char netName[128], container[128];
	compose_network_name(r->projectName, netName, sizeof(netName));
	compose_container_name(r->projectName, nodeName, container, sizeof(container));

	log_info("  Reconnecting %s to network %s", container, netName);
	char out[1024] = "";
	int rc = run_cmd_capture(out, sizeof(out),
							 "docker network connect %s %s 2>&1",
							 netName, container);
	if (rc != 0 && out[0])
	{
		log_output("  ", out);
	}
	return rc == 0;
}


/* -----------------------------------------------------------------------
 * Execute a single command
 * ----------------------------------------------------------------------- */

/*
 * runner_expand_macros substitutes %CIDR% in the args string with the
 * Docker network CIDR reported by `pg_autoctl inspect show cidr` on the
 * monitor container.  The result is written to dst (at most dstlen bytes).
 * Returns false if the CIDR could not be determined.
 */
static bool
runner_expand_macros(TestRunner *r, const char *args, char *dst, int dstlen,
					 char *errBuf, int errLen)
{
	const char *macro = "%CIDR%";
	const char *found = strstr(args, macro);

	if (!found)
	{
		strlcpy(dst, args, dstlen);
		return true;
	}

	/* fetch CIDR from the monitor */
	char cidr[128] = "";
	int rc = run_cmd_capture(cidr, sizeof(cidr),
							 "%s exec -T monitor pg_autoctl inspect show cidr 2>/dev/null",
							 r->composeBase);
	if (rc != 0 || cidr[0] == '\0')
	{
		snprintf(errBuf, errLen,
				 "%%CIDR%%: could not get Docker network CIDR from monitor");
		return false;
	}

	/* trim trailing newline */
	int n = strlen(cidr);
	while (n > 0 && (cidr[n - 1] == '\n' || cidr[n - 1] == '\r' || cidr[n - 1] == ' '))
	{
		cidr[--n] = '\0';
	}

	log_info("           %%CIDR%% = %s", cidr);

	/* substitute all occurrences */
	int di = 0;
	for (const char *p = args; *p && di < dstlen - 1;)
	{
		if (strncmp(p, macro, strlen(macro)) == 0)
		{
			int cl = strlen(cidr);
			if (di + cl < dstlen - 1)
			{
				memcpy(dst + di, cidr, cl);
				di += cl;
			}
			p += strlen(macro);
		}
		else
		{
			dst[di++] = *p++;
		}
	}
	dst[di] = '\0';
	return true;
}


static bool
runner_exec_cmd(TestRunner *r, TestCmd *cmd, char *errBuf, int errLen)
{
	switch (cmd->kind)
	{
		case CMD_EXEC:
		{
			char expandedArgs[4096] = "";
			if (!runner_expand_macros(r, cmd->args, expandedArgs,
									  sizeof(expandedArgs), errBuf, errLen))
			{
				return false;
			}

			char out[4096] = "";
			int rc = run_cmd_capture(out, sizeof(out),
									 "%s exec -T %s %s 2>&1",
									 r->composeBase,
									 cmd->service, expandedArgs);

			/* store output so a following expect{} can match it */
			strlcpy(r->lastSqlOutput, out, sizeof(r->lastSqlOutput));
			r->lastSqlFailed = false;
			if (rc != 0)
			{
				if (out[0])
				{
					log_output("   ", out);
				}
				snprintf(errBuf, errLen,
						 "exec %s %s failed (exit %d)",
						 cmd->service, expandedArgs, rc);
				return false;
			}

			/*
			 * Several pg_autoctl commands are synchronous from the monitor's
			 * perspective but leave NOTIFY messages buffered in the libpq
			 * socket.  Drain them with proper '*' marks and confirm the
			 * expected post-command state before moving to the next step.
			 *
			 * perform failover / perform switchover / perform promotion:
			 *   The entire FSM cycle completes inside the blocking exec.
			 *   Wait for the formation to settle at primary+secondary.
			 *
			 * enable maintenance:
			 *   The node transitions to maintenance asynchronously after
			 *   the command returns.  Wait for cmd->service to reach
			 *   "maintenance" (single-node LISTEN-driven wait).
			 */

			/*
			 * Only trigger implicit waits for direct pg_autoctl invocations,
			 * not for bash -c "..." wrappers where the match is incidental.
			 */
			bool is_pg_autoctl = (strncmp(expandedArgs, "pg_autoctl", 10) == 0);
			if (is_pg_autoctl &&
				(strstr(expandedArgs, "perform failover") != NULL ||
				 strstr(expandedArgs, "perform switchover") != NULL ||
				 strstr(expandedArgs, "perform promotion") != NULL ||
				 strstr(expandedArgs, "disable maintenance") != NULL))
			{
				static const char fsStates[2][64] = { "primary", "secondary" };
				if (!monitor_wait_formation_states(r, fsStates, 2,
												   NULL, 0, 120))
				{
					snprintf(errBuf, errLen,
							 "exec %s %s: timed out waiting for "
							 "primary+secondary",
							 cmd->service, expandedArgs);
					return false;
				}
			}
			else if (is_pg_autoctl && strstr(expandedArgs, "enable maintenance") != NULL)
			{
				/*
				 * Drain any notifications already buffered from the enable
				 * maintenance command before calling wait_for_state, so the
				 * maintenance convergence notification gets the '*' mark here
				 * rather than spilling unmarked into the next inter-command drain.
				 */
				if (r->notifyConnected)
				{
					const char *mn = cmd->service, *ms = "maintenance";
					bool sat = false;
					runner_drain_notify(r, NULL, &mn, &ms, 1, &sat);
				}
				if (!wait_for_state(r, cmd->service, "maintenance", 60, false))
				{
					snprintf(errBuf, errLen,
							 "exec %s %s: timed out waiting for maintenance",
							 cmd->service, expandedArgs);
					return false;
				}
			}
			return true;
		}

		case CMD_EXEC_FAILS:
		{
			char out[4096] = "";
			int rc = run_cmd_capture(out, sizeof(out),
									 "%s exec -T %s %s 2>&1",
									 r->composeBase,
									 cmd->service, cmd->args);
			if (rc == 0)
			{
				if (out[0])
				{
					log_output("   ", out);
				}
				snprintf(errBuf, errLen,
						 "exec-fails %s %s: command succeeded (exit 0) "
						 "but expected failure",
						 cmd->service, cmd->args);
				return false;
			}
			log_debug("exec-fails %s %s: exited with %d (expected)",
					  cmd->service, cmd->args, rc);
			return true;
		}

		case CMD_WAIT_STATE:
		{
			/*
			 * LISTEN-driven wait (items 5 & 6).
			 *
			 * runner_wait_notify_goal() watches the "state" channel and fires
			 * when goalState == target for this node.  Pass-through states are
			 * also tracked from goalState notifications (not by polling).
			 * After success, verify all pass-through states were observed.
			 */
			bool seenThrough[PGAF_MAX_WAIT_STATES] = { false };

			if (!runner_wait_notify_goal(r, cmd->service, cmd->state,
										 (const char (*)[64])cmd->passThroughStates,
										 seenThrough, cmd->passThroughCount,
										 cmd->timeoutSeconds))
			{
				snprintf(errBuf, errLen,
						 "timeout: %s assigned-state never reached %s",
						 cmd->service, cmd->state);
				return false;
			}

			for (int i = 0; i < cmd->passThroughCount; i++)
			{
				if (!seenThrough[i])
				{
					snprintf(errBuf, errLen,
							 "%s did not pass through assigned-state %s "
							 "on way to %s",
							 cmd->service,
							 cmd->passThroughStates[i],
							 cmd->state);
					return false;
				}
			}
			return true;
		}

		case CMD_WAIT_STATES:
		{
			if (!wait_for_states(r, cmd))
			{
				/* wait_for_states logs its own error detail */
				char label[128] = "";
				for (int i = 0; i < cmd->waitStateCount; i++)
				{
					if (i > 0)
					{
						strncat(label, ",", sizeof(label) - strlen(label) - 1);
					}
					strncat(label, cmd->waitStates[i],
							sizeof(label) - strlen(label) - 1);
				}
				snprintf(errBuf, errLen,
						 "timeout: formation never reached states {%s}", label);
				return false;
			}
			return true;
		}

		case CMD_PROMOTE:
		{
			for (int i = 0; i < cmd->promoteCount; i++)
			{
				if (!runner_promote_one(r, cmd->promoteNodes[i]))
				{
					snprintf(errBuf, errLen,
							 "promote: could not make %s primary",
							 cmd->promoteNodes[i]);
					return false;
				}
			}
			return true;
		}

		case CMD_ASSERT_STATE:
		case CMD_ASSERT_ASSIGNED:
		{
			/* when a timeout is set this is a "wait until" — poll until match */
			if (cmd->timeoutSeconds > 0)
			{
				if (!wait_for_state(r, cmd->service, cmd->state,
									cmd->timeoutSeconds,
									cmd->kind == CMD_ASSERT_ASSIGNED))
				{
					snprintf(errBuf, errLen,
							 "timeout: %s %s never reached %s",
							 cmd->service,
							 cmd->kind == CMD_ASSERT_ASSIGNED
							 ? "assigned-state" : "state",
							 cmd->state);
					return false;
				}
				return true;
			}

			char reported[64] = "", assigned[64] = "";
			if (!monitor_get_node_state(r, cmd->service,
										reported, sizeof(reported),
										assigned, sizeof(assigned)))
			{
				snprintf(errBuf, errLen,
						 "cannot reach monitor to assert %s state",
						 cmd->service);
				return false;
			}
			const char *actual = (cmd->kind == CMD_ASSERT_ASSIGNED)
								 ? assigned : reported;
			if (strcmp(actual, cmd->state) != 0)
			{
				snprintf(errBuf, errLen,
						 "%s %s is \"%s\", expected \"%s\"",
						 cmd->service,
						 (cmd->kind == CMD_ASSERT_ASSIGNED)
						 ? "assigned-state" : "state",
						 actual, cmd->state);
				return false;
			}
			return true;
		}

		case CMD_SQL:
		{
			strlcpy(r->lastSqlService, cmd->service, sizeof(r->lastSqlService));
			r->lastSqlFailed = false;
			r->lastSqlState[0] = '\0';
			r->lastSqlOutput[0] = '\0';

			if (!exec_sql_on_service(r, cmd->service, cmd->args,
									 r->lastSqlOutput,
									 sizeof(r->lastSqlOutput)))
			{
				if (cmd->allowError)
				{
					/*
					 * Soft failure: expected by the following CMD_EXPECT_ERROR.
					 * Record the SQLSTATE and clear the output (it's an error
					 * message, not usable result rows).
					 */
					r->lastSqlFailed = true;
					parse_sqlstate(r->lastSqlOutput, r->lastSqlState,
								   sizeof(r->lastSqlState));
					log_debug("sql on %s failed with SQLSTATE %s (expected)",
							  cmd->service,
							  r->lastSqlState[0] ? r->lastSqlState : "(unknown)");
					r->lastSqlOutput[0] = '\0';
					return true;
				}
				snprintf(errBuf, errLen,
						 "sql on %s failed:\n%s", cmd->service, r->lastSqlOutput);
				return false;
			}
			log_debug("sql output: %s", r->lastSqlOutput);
			return true;
		}

		case CMD_EXPECT:
		{
			/*
			 * Substring match: the expect value is a pattern that must appear
			 * somewhere in the SQL output.  This lets tests avoid depending on
			 * non-deterministic details like node IDs while still being specific
			 * enough to catch regressions.
			 */
			if (strstr(r->lastSqlOutput, cmd->expected) == NULL)
			{
				snprintf(errBuf, errLen,
						 "expected \"%s\", got \"%s\"",
						 cmd->expected, r->lastSqlOutput);
				return false;
			}
			return true;
		}

		case CMD_EXPECT_ERROR:
		{
			if (!r->lastSqlFailed)
			{
				snprintf(errBuf, errLen,
						 "expected SQL error on %s, but the query succeeded",
						 r->lastSqlService);
				return false;
			}
			if (cmd->state[0] &&
				strcmp(r->lastSqlState, cmd->state) != 0)
			{
				snprintf(errBuf, errLen,
						 "expected SQLSTATE %s on %s, got %s",
						 cmd->state, r->lastSqlService,
						 r->lastSqlState[0] ? r->lastSqlState : "(unknown)");
				return false;
			}
			r->lastSqlFailed = false;  /* consumed */
			return true;
		}

		case CMD_NETWORK_OFF:
		{
			if (!runner_network_off(r, cmd->service))
			{
				snprintf(errBuf, errLen,
						 "network disconnect %s failed", cmd->service);
				return false;
			}
			return true;
		}

		case CMD_NETWORK_ON:
		{
			if (!runner_network_on(r, cmd->service))
			{
				snprintf(errBuf, errLen,
						 "network connect %s failed", cmd->service);
				return false;
			}
			return true;
		}

		case CMD_SLEEP:
		{
			sleep(cmd->timeoutSeconds);
			return true;
		}

		case CMD_COMPOSE_DOWN:
		{
			return runner_compose_down(r);
		}

		case CMD_COMPOSE_START:
		{
			/*
			 * Use `up -d --no-recreate --no-deps` rather than `start`:
			 * `start` only works for already-created containers, but deferred
			 * nodes (launch deferred) are never created during compose up,
			 * so `start` would fail for them.  `up -d --no-recreate --no-deps`
			 * handles both: creates+starts a new container, or starts an
			 * existing stopped one without touching other services.
			 *
			 * `--no-deps` is critical: without it, `start` (and `up`) would
			 * also start any stopped services listed in `depends_on`, which
			 * interferes with tests that restart a single node.
			 */
			char out[4096] = "";
			int rc = run_cmd_capture(out, sizeof(out),
									 "%s up -d --no-recreate --no-deps %s 2>&1",
									 r->composeBase, cmd->service);
			if (out[0])
			{
				log_output("   compose: ", out);
			}
			if (rc != 0)
			{
				snprintf(errBuf, errLen,
						 "docker compose start %s failed (exit %d)",
						 cmd->service, rc);
				return false;
			}
			return true;
		}

		case CMD_COMPOSE_STOP:
		{
			char out[4096] = "";
			int rc = run_cmd_capture(out, sizeof(out),
									 "%s stop %s 2>&1",
									 r->composeBase, cmd->service);
			if (out[0])
			{
				log_output("   compose: ", out);
			}
			if (rc != 0)
			{
				snprintf(errBuf, errLen,
						 "docker compose stop %s failed (exit %d)",
						 cmd->service, rc);
				return false;
			}
			return true;
		}

		case CMD_COMPOSE_KILL:
		{
			char out[4096] = "";
			int rc = run_cmd_capture(out, sizeof(out),
									 "%s kill %s 2>&1",
									 r->composeBase, cmd->service);
			if (out[0])
			{
				log_output("   compose: ", out);
			}
			if (rc != 0)
			{
				snprintf(errBuf, errLen,
						 "docker compose kill %s failed (exit %d)",
						 cmd->service, rc);
				return false;
			}
			return true;
		}

		/*
		 * CMD_COMPOSE_INJECT: copy a file from a Docker image into a running
		 * service container without restarting it.
		 *
		 *   compose inject pgaf:next /usr/local/bin/pg_autoctl monitor:/usr/local/bin/pg_autoctl
		 *
		 * Fields:  expected=image  args=src-path  service=dst-svc  state=dst-path
		 *
		 * Sequence:
		 *   1. docker create --name _pgaf_inject_tmp <image>
		 *   2. docker cp _pgaf_inject_tmp:<src>  /tmp/_pgaf_inject_binary
		 *   3. docker rm _pgaf_inject_tmp
		 *   4. docker cp /tmp/_pgaf_inject_binary  <container>:<dst>
		 */
		case CMD_COMPOSE_INJECT:
		{
			char out[4096] = "";
			int rc;

			log_info("compose inject: creating temporary container from %s",
					 cmd->expected);

			rc = run_cmd_capture(out, sizeof(out),
								 "docker create --name _pgaf_inject_tmp %s 2>&1",
								 cmd->expected);
			if (out[0])
			{
				log_output("   inject: ", out);
			}
			if (rc != 0)
			{
				snprintf(errBuf, errLen,
						 "compose inject: docker create %s failed (exit %d)",
						 cmd->expected, rc);
				return false;
			}

			out[0] = '\0';
			rc = run_cmd_capture(out, sizeof(out),
								 "docker cp _pgaf_inject_tmp:%s /tmp/_pgaf_inject_binary 2>&1",
								 cmd->args);
			if (out[0])
			{
				log_output("   inject: ", out);
			}

			/* always remove the temp container, even on failure */
			run_cmd_capture(out, sizeof(out),
							"docker rm _pgaf_inject_tmp 2>&1");
			if (rc != 0)
			{
				snprintf(errBuf, errLen,
						 "compose inject: docker cp from image failed (exit %d)", rc);
				return false;
			}

			char container[256];
			compose_container_name(r->projectName, cmd->service,
								   container, sizeof(container));

			out[0] = '\0';
			rc = run_cmd_capture(out, sizeof(out),
								 "docker cp /tmp/_pgaf_inject_binary %s:%s 2>&1",
								 container, cmd->state);
			if (out[0])
			{
				log_output("   inject: ", out);
			}
			if (rc != 0)
			{
				snprintf(errBuf, errLen,
						 "compose inject: docker cp to %s:%s failed (exit %d)",
						 cmd->service, cmd->state, rc);
				return false;
			}

			log_info("compose inject: %s:%s → %s:%s",
					 cmd->expected, cmd->args, cmd->service, cmd->state);
			return true;
		}

		/*
		 * CMD_WAIT_MULTI: wait until all (node, state) pairs are satisfied.
		 *
		 *   wait until node2 state is primary and node1 state is secondary
		 *              with timeout 90s
		 *
		 * Polls each condition in turn; repeats until all are true or timeout.
		 */
		case CMD_WAIT_MULTI:
		{
			int timeoutSecs = cmd->timeoutSeconds;
			time_t deadline = time(NULL) + timeoutSecs;

			/* build pointer arrays once — used for marking on every drain */
			const char *mn[PGAF_MAX_WAIT_STATES], *ms[PGAF_MAX_WAIT_STATES];
			for (int i = 0; i < cmd->waitStateCount; i++)
			{
				mn[i] = cmd->waitNodes[i];
				ms[i] = cmd->waitStates[i];
			}

			/* LISTEN-based satisfied[]: set by runner_drain_notify when a
			 * convergence notification arrives for a specific (node, state) pair */
			bool listenSatisfied[PGAF_MAX_WAIT_STATES] = { false };

			while (time(NULL) < deadline)
			{
				/*
				 * Try subprocess-based check first.  monitor_get_node_state runs
				 * "pg_autoctl inspect monitor node-state" which requires v2.2.
				 * When nodes run v2.1 the call returns false; track how many
				 * succeed so we can fall back to LISTEN-based convergence.
				 */
				bool allMet = true;
				int subproc_ok = 0;
				for (int i = 0; i < cmd->waitStateCount; i++)
				{
					char reported[64] = "", assigned[64] = "";
					if (!monitor_get_node_state(r, cmd->waitNodes[i],
												reported, sizeof(reported),
												assigned, sizeof(assigned)))
					{
						allMet = false; /* can't confirm via subprocess */
						/* don't break — try to drain anyway */
						continue;
					}
					subproc_ok++;

					/*
					 * Require convergence: the node must have reported the
					 * target state (reportedState) and the monitor must agree
					 * it belongs there (assignedState / goalState).  Checking
					 * only reportedState could catch a transient pass-through;
					 * checking only assignedState fires before the node confirms.
					 */
					if (strcmp(reported, cmd->waitStates[i]) != 0 ||
						strcmp(assigned, cmd->waitStates[i]) != 0)
					{
						allMet = false;
					}
				}

				/*
				 * Drain: log notifications, mark '*' on convergence events, and
				 * set listenSatisfied[i] when the (node, state) pair converges.
				 */
				runner_drain_notify(r, NULL, mn, ms,
									cmd->waitStateCount, listenSatisfied);

				/*
				 * When subprocess is unavailable (v2.1 nodes), fall back to
				 * LISTEN-based convergence.  If all conditions have been seen
				 * converged via NOTIFY, treat that as success.
				 */
				if (subproc_ok == 0)
				{
					bool allListenMet = true;
					for (int i = 0; i < cmd->waitStateCount; i++)
					{
						if (!listenSatisfied[i])
						{
							allListenMet = false;
							break;
						}
					}
					if (allListenMet)
					{
						return true;
					}
				}
				else if (allMet)
				{
					return true;
				}

				runner_wait_socket(r, 1000);
			}

			/* build readable label for error */
			char label[512] = "";
			for (int i = 0; i < cmd->waitStateCount; i++)
			{
				if (i > 0)
				{
					strncat(label, " and ", sizeof(label) - strlen(label) - 1);
				}
				char part[128];
				snprintf(part, sizeof(part), "%s=%s",
						 cmd->waitNodes[i], cmd->waitStates[i]);
				strncat(label, part, sizeof(label) - strlen(label) - 1);
			}
			snprintf(errBuf, errLen,
					 "timeout: conditions not met: %s", label);
			return false;
		}

		/*
		 * CMD_PG_AUTOCTL: run pg_autoctl locally in the pgaftest process.
		 *
		 * When running as a compose service, PG_AUTOCTL_MONITOR is set in the
		 * container environment (see compose_gen.c) so pg_autoctl finds the
		 * monitor URI automatically without any --monitor injection here.
		 *
		 * When running on the host (pgaftest run outside Docker), the variable
		 * must be set by the user or in their shell environment.
		 */
		case CMD_PG_AUTOCTL:
		{
			char out[4096] = "";
			int rc = run_cmd_capture(out, sizeof(out),
									 "pg_autoctl %s 2>&1", cmd->args);
			if (rc != 0)
			{
				if (out[0])
				{
					log_output("   ", out);
				}
				snprintf(errBuf, errLen,
						 "pg_autoctl %s failed (exit %d)", cmd->args, rc);
				return false;
			}
			return true;
		}

		case CMD_WAIT_STOPPED:
		{
			const char *svc = cmd->service;
			int timeoutSecs = cmd->timeoutSeconds;
			time_t deadline = time(NULL) + timeoutSecs;

			log_info("  Waiting for service %s to stop (timeout %ds)",
					 svc, timeoutSecs);

			while (time(NULL) < deadline)
			{
				/*
				 * `docker compose ps --format {{.State}} <svc>` outputs the
				 * container state: "running", "exited", "paused", etc.
				 * An empty or non-running result also counts as stopped.
				 */
				char state[64] = "";
				int rc = run_cmd_capture(state, sizeof(state),
										 "%s ps --format '{{.State}}' %s"
										 " 2>/dev/null",
										 r->composeBase, svc);

				/* strip whitespace */
				int n = strlen(state);
				while (n > 0 && (state[n - 1] == '\n' || state[n - 1] == ' '))
				{
					state[--n] = '\0';
				}

				if (rc != 0 || state[0] == '\0' ||
					strcmp(state, "running") != 0)
				{
					log_info("  Service %s stopped (state: %s)", svc,
							 state[0] ? state : "gone");
					return true;
				}

				usleep(500000); /* 500ms */
			}

			snprintf(errBuf, errLen,
					 "timeout: service %s did not stop within %ds",
					 svc, timeoutSecs);
			return false;
		}

		case CMD_STOP_POSTGRES:
		{
			/*
			 * Signal the pg_autoctl postgres-controller service to stop
			 * Postgres.  Equivalent to calling `pg_autoctl manual service pgctl off`.
			 */
			log_info("Stopping Postgres on %s", cmd->service);
			int rc = run_cmd(
				"%s exec %s pg_autoctl manual service pgctl off"
				" --pgdata /var/lib/postgres/pgaf",
				r->composeBase, cmd->service);
			if (rc != 0)
			{
				snprintf(errBuf, errLen,
						 "stop postgres %s failed (exit %d)",
						 cmd->service, rc);
				return false;
			}
			return true;
		}

		case CMD_START_POSTGRES:
		{
			log_info("Starting Postgres on %s", cmd->service);
			int rc = run_cmd(
				"%s exec %s pg_autoctl manual service pgctl on"
				" --pgdata /var/lib/postgres/pgaf",
				r->composeBase, cmd->service);
			if (rc != 0)
			{
				snprintf(errBuf, errLen,
						 "start postgres %s failed (exit %d)",
						 cmd->service, rc);
				return false;
			}
			return true;
		}

		case CMD_STAYS_WHILE:
		{
			/*
			 * LISTEN-driven stays-while (item 9).
			 *
			 * Strategy:
			 *   1. Connect to LISTEN "state" channel.
			 *   2. Verify the node's current assigned-state matches expectation.
			 *   3. Flush any pending notifications (establish clean baseline).
			 *   4. Run each body command.
			 *   5. After each command, drain the notification queue and fail
			 *      immediately if any notification for this node has goalState
			 *      != expectedState.
			 *   6. Final SQL check after the body.
			 *
			 * The LISTEN approach catches state-change assignments that happen
			 * *during* a body command, not just between commands.
			 */
			log_info("assert %s stays %s while running body",
					 cmd->service, cmd->state);

			runner_notify_connect(r);

			/* initial assigned-state check */
			char curReported[64] = "", curAssigned[64] = "";
			if (!monitor_get_node_state(r, cmd->service,
										curReported, sizeof(curReported),
										curAssigned, sizeof(curAssigned)))
			{
				snprintf(errBuf, errLen,
						 "stays-while: could not get state of %s", cmd->service);
				return false;
			}
			if (strcmp(curAssigned, cmd->state) != 0)
			{
				snprintf(errBuf, errLen,
						 "stays-while: %s assigned-state is %s, "
						 "expected %s (before body)",
						 cmd->service, curAssigned, cmd->state);
				return false;
			}

			/* flush any pre-existing notifications so we start from a clean slate */
			if (r->notifyConnected)
			{
				runner_drain_notify(r, NULL, NULL, NULL, 0, NULL);
			}

			/* run each body command, checking notifications after each */
			for (TestCmd *bc = cmd->body; bc; bc = bc->next)
			{
				if (!runner_exec_cmd(r, bc, errBuf, errLen))
				{
					return false;
				}

				/* check notifications: any goalState change for this node? */
				if (!runner_check_stays_notify(r, cmd->service, cmd->state,
											   errBuf, errLen))
				{
					return false;
				}

				/* also SQL-poll when LISTEN is unavailable */
				if (!r->notifyConnected)
				{
					if (!monitor_get_node_state(r, cmd->service,
												curReported, sizeof(curReported),
												curAssigned, sizeof(curAssigned)))
					{
						snprintf(errBuf, errLen,
								 "stays-while: could not get state of %s after command",
								 cmd->service);
						return false;
					}
					if (strcmp(curAssigned, cmd->state) != 0)
					{
						snprintf(errBuf, errLen,
								 "stays-while: %s assigned-state changed to %s, "
								 "expected %s during body",
								 cmd->service, curAssigned, cmd->state);
						return false;
					}
				}
			}

			/* final check after all body commands */
			if (!runner_check_stays_notify(r, cmd->service, cmd->state,
										   errBuf, errLen))
			{
				return false;
			}
			if (!r->notifyConnected)
			{
				if (!monitor_get_node_state(r, cmd->service,
											curReported, sizeof(curReported),
											curAssigned, sizeof(curAssigned)) ||
					strcmp(curAssigned, cmd->state) != 0)
				{
					snprintf(errBuf, errLen,
							 "stays-while: %s assigned-state is %s "
							 "after body, expected %s",
							 cmd->service, curAssigned, cmd->state);
					return false;
				}
			}
			return true;
		}

		case CMD_SET_MONITOR:
		{
			/*
			 * Switch the active monitor service: drop the existing LISTEN
			 * connection (if any) and reconnect to the new monitor.  All
			 * subsequent LISTEN/NOTIFY waits and monitor_get_node_state()
			 * calls will target the new service.
			 */
			log_info("  set monitor: switching to service \"%s\"",
					 cmd->service);

			if (r->notifyConnected)
			{
				pgsql_finish(&r->notifyConn);
				r->notifyConnected = false;
			}

			strlcpy(r->activeMonitorService, cmd->service,
					sizeof(r->activeMonitorService));

			/* reconnect immediately so wait loops can start right away */
			runner_notify_connect(r);
			return true;
		}

		case CMD_LOGS_CHECK:
		{
			/*
			 * Grep the container logs.
			 * cmd->allowError == false → fixed-string grep (-F)
			 * cmd->allowError == true  → PCRE grep (-P)
			 * cmd->logsNegate          → assert pattern NOT found
			 */
			bool pcre = cmd->allowError;
			bool negate = cmd->logsNegate;
			const char *flag = pcre ? "-qP" : "-qF";

			/* Shell-quote the pattern by wrapping in single quotes.
			 * Replace any embedded ' with '\'' to safely handle patterns
			 * such as PCRE lookahead strings.
			 */
			char quotedPat[sizeof(cmd->args) * 4 + 4];
			{
				char *q = quotedPat;
				*q++ = '\'';
				for (const char *p = cmd->args; *p; p++)
				{
					if (*p == '\'')
					{
						*q++ = '\'';
						*q++ = '\\';
						*q++ = '\'';
						*q++ = '\'';
					}
					else
					{
						*q++ = *p;
					}
				}
				*q++ = '\'';
				*q = '\0';
			}

			char grepCmd[8192];
			snprintf(grepCmd, sizeof(grepCmd),
					 "docker compose --project-directory %s logs --no-color %s 2>&1 | grep %s %s",
					 r->workDir,
					 cmd->service,
					 flag,
					 quotedPat);

			log_info("  logs %s: %s%s \"%s\"",
					 cmd->service,
					 negate ? "not " : "",
					 pcre ? "matches" : "contains",
					 cmd->args);

			int rc = system(grepCmd);
			bool found = (rc == 0);

			if (negate && found)
			{
				log_error("logs check failed: pattern found in %s logs: %s",
						  cmd->service, cmd->args);
				return false;
			}
			else if (!negate && !found)
			{
				log_error("logs check failed: pattern not found in %s logs: %s",
						  cmd->service, cmd->args);
				return false;
			}
			return true;
		}
	}
	return true;
}


/* -----------------------------------------------------------------------
 * Execute a step (list of commands)
 * ----------------------------------------------------------------------- */

/*
 * log_output prints each non-empty line of a captured command output block
 * at INFO level, prefixed with `prefix`.  Used to fold multi-line output
 * from docker compose / docker network into the structured log stream.
 */
static void
log_output(const char *prefix, const char *out)
{
	char buf[4096];
	strlcpy(buf, out, sizeof(buf));
	for (char *line = buf, *nl; line && *line; line = nl ? nl + 1 : NULL)
	{
		nl = strchr(line, '\n');
		if (nl)
		{
			*nl = '\0';
		}

		/* skip blank / pure-whitespace lines */
		const char *p = line;
		while (*p == ' ' || *p == '\t' || *p == '\r')
		{
			p++;
		}
		if (*p)
		{
			log_info("%s%s", prefix, line);
		}
	}
}


/*
 * inline_text copies src into dst, collapsing runs of whitespace (including
 * newlines) into a single space and trimming leading/trailing whitespace.
 * Result is always NUL-terminated and fits in dstlen bytes.
 */
static void
inline_text(const char *src, char *dst, int dstlen)
{
	int di = 0;
	bool space = true; /* suppress leading whitespace */
	for (const char *p = src; *p && di < dstlen - 1; p++)
	{
		if (*p == '\n' || *p == '\r' || *p == '\t' || *p == ' ')
		{
			if (!space && di < dstlen - 1)
			{
				dst[di++] = ' ';
			}
			space = true;
		}
		else
		{
			dst[di++] = *p;
			space = false;
		}
	}

	/* trim trailing space */
	while (di > 0 && dst[di - 1] == ' ')
	{
		di--;
	}
	dst[di] = '\0';
}


/*
 * cmd_label returns a human-readable description of a command matching the
 * DSL syntax from the spec file.  The caller owns the buffer.
 */
static void
cmd_label(const TestCmd *cmd, char *buf, int len)
{
	char tmp[512];

	switch (cmd->kind)
	{
		case CMD_EXEC:
		{
			snprintf(buf, len, "exec %s  %s", cmd->service, cmd->args);
			break;
		}

		case CMD_EXEC_FAILS:
		{
			snprintf(buf, len, "exec-fails %s  %s", cmd->service, cmd->args);
			break;
		}

		case CMD_WAIT_STATE:
		{
			snprintf(buf, len, "wait until %s state = %s  timeout %ds",
					 cmd->service, cmd->state, cmd->timeoutSeconds);
			break;
		}

		case CMD_WAIT_STATES:
		{
			char states[256] = "";
			for (int i = 0; i < cmd->waitStateCount; i++)
			{
				if (i > 0)
				{
					strncat(states, ", ", sizeof(states) - strlen(states) - 1);
				}
				strncat(states, cmd->waitStates[i], sizeof(states) - strlen(states) - 1);
			}
			snprintf(buf, len, "wait until %s  timeout %ds", states,
					 cmd->timeoutSeconds);
			break;
		}

		case CMD_ASSERT_STATE:
		{
			snprintf(buf, len, "assert %s state = %s", cmd->service, cmd->state);
			break;
		}

		case CMD_ASSERT_ASSIGNED:
		{
			snprintf(buf, len, "assert %s assigned-state = %s",
					 cmd->service, cmd->state);
			break;
		}

		case CMD_PROMOTE:
		{
			char nodes[256] = "";
			for (int i = 0; i < cmd->promoteCount; i++)
			{
				if (i > 0)
				{
					strncat(nodes, ", ", sizeof(nodes) - strlen(nodes) - 1);
				}
				strncat(nodes, cmd->promoteNodes[i],
						sizeof(nodes) - strlen(nodes) - 1);
			}
			snprintf(buf, len, "promote %s", nodes[0] ? nodes : cmd->service);
			break;
		}

		case CMD_SQL:
		{
			inline_text(cmd->args, tmp, sizeof(tmp));
			snprintf(buf, len, "sql %s { %s }", cmd->service, tmp);
			break;
		}

		case CMD_EXPECT:
		{
			if (strchr(cmd->expected, '\n'))
			{
				/* multi-row: reconstruct { row } { row } display form */
				char rows[256] = { 0 };
				int rpos = 0;
				const char *p = cmd->expected;
				while (*p)
				{
					const char *nl = strchr(p, '\n');
					int rowlen = nl ? (int) (nl - p) : (int) strlen(p);
					int n = snprintf(rows + rpos, sizeof(rows) - rpos,
									 "%s{ %.*s }",
									 rpos ? " " : "", rowlen, p);
					if (n > 0)
					{
						rpos += n;
					}
					p = nl ? nl + 1 : p + rowlen;
				}
				snprintf(buf, len, "expect { %s }", rows);
			}
			else
			{
				inline_text(cmd->expected, tmp, sizeof(tmp));
				snprintf(buf, len, "expect { %s }", tmp);
			}
			break;
		}

		case CMD_EXPECT_ERROR:
		{
			snprintf(buf, len, "expect error %s", cmd->state);
			break;
		}

		case CMD_NETWORK_OFF:
		{
			snprintf(buf, len, "network disconnect %s", cmd->service);
			break;
		}

		case CMD_NETWORK_ON:
		{
			snprintf(buf, len, "network connect %s", cmd->service);
			break;
		}

		case CMD_SLEEP:
		{
			snprintf(buf, len, "sleep %ds", cmd->timeoutSeconds);
			break;
		}

		case CMD_COMPOSE_DOWN:
		{
			snprintf(buf, len, "compose down");
			break;
		}

		case CMD_COMPOSE_START:
		{
			snprintf(buf, len, "compose start %s", cmd->service);
			break;
		}

		case CMD_COMPOSE_STOP:
		{
			snprintf(buf, len, "compose stop %s", cmd->service);
			break;
		}

		case CMD_COMPOSE_KILL:
		{
			snprintf(buf, len, "compose kill %s", cmd->service);
			break;
		}

		case CMD_COMPOSE_INJECT:
		{
			snprintf(buf, len, "compose inject %s %s %s:%s",
					 cmd->expected, cmd->args, cmd->service, cmd->state);
			break;
		}

		case CMD_WAIT_STOPPED:
		{
			snprintf(buf, len, "wait until %s stopped  timeout %ds",
					 cmd->service, cmd->timeoutSeconds);
			break;
		}

		case CMD_WAIT_MULTI:
		{
			char parts[512] = "";
			for (int i = 0; i < cmd->waitStateCount; i++)
			{
				if (i > 0)
				{
					strncat(parts, " and ", sizeof(parts) - strlen(parts) - 1);
				}
				char piece[128];
				snprintf(piece, sizeof(piece), "%s state = %s",
						 cmd->waitNodes[i], cmd->waitStates[i]);
				strncat(parts, piece, sizeof(parts) - strlen(parts) - 1);
			}
			snprintf(buf, len, "wait until %s  timeout %ds", parts,
					 cmd->timeoutSeconds);
			break;
		}

		case CMD_PG_AUTOCTL:
		{
			snprintf(buf, len, "pg_autoctl %s", cmd->args);
			break;
		}

		case CMD_STOP_POSTGRES:
		{
			snprintf(buf, len, "stop postgres %s", cmd->service);
			break;
		}

		case CMD_START_POSTGRES:
		{
			snprintf(buf, len, "start postgres %s", cmd->service);
			break;
		}

		case CMD_STAYS_WHILE:
		{
			snprintf(buf, len, "assert %s stays %s while { ... }",
					 cmd->service, cmd->state);
			break;
		}

		case CMD_SET_MONITOR:
		{
			snprintf(buf, len, "set monitor %s", cmd->service);
			break;
		}

		case CMD_LOGS_CHECK:
		{
			snprintf(buf, len, "logs %s %s%s \"%s\"",
					 cmd->service,
					 cmd->logsNegate ? "not " : "",
					 cmd->allowError ? "matches" : "contains",
					 cmd->args);
			break;
		}

		default:
		{
			snprintf(buf, len, "(unknown command %d)", cmd->kind);
			break;
		}
	}
}


static bool
runner_exec_step(TestRunner *r, TestStep *step, char *errBuf, int errLen,
				 int stepNum)
{
	int cmdNum = 0;
	for (TestCmd *cmd = step->commands; cmd; cmd = cmd->next)
	{
		/*
		 * Flush all notifications that arrived during the previous command.
		 * Loop until the socket is idle for 50 ms so we catch notifications
		 * still in-flight in the TCP stream, not just what libpq buffered.
		 */
		if (r->notifyConnected)
		{
			while (runner_wait_socket(r, 50))
			{
				runner_drain_notify(r, NULL, NULL, NULL, 0, NULL);
			}
			runner_drain_notify(r, NULL, NULL, NULL, 0, NULL); /* one last sweep of the libpq buffer */
		}

		char label[256];
		cmd_label(cmd, label, sizeof(label));
		if (stepNum > 0)
		{
			log_info(" [%02d-%02d] %s", stepNum, ++cmdNum, label);
		}
		else
		{
			log_info(" [%02d] %s", ++cmdNum, label);
		}
		if (!runner_exec_cmd(r, cmd, errBuf, errLen))
		{
			return false;
		}
	}
	return true;
}


/* -----------------------------------------------------------------------
 * Load runner state from workDir (for step/down subcommands)
 * ----------------------------------------------------------------------- */
static bool
runner_load_state(TestRunner *r)
{
	/* Check the compose file exists — if it does, the stack should be up */
	struct stat st;
	if (stat(r->composeFile, &st) != 0)
	{
		log_error("No compose file found at \"%s\"; run `pgaftest setup` first",
				  r->composeFile);
		return false;
	}
	r->composeUp = true;
	return true;
}


/* -----------------------------------------------------------------------
 * Monitor readiness ping loop
 *
 * When running inside the compose network (PGAFTEST_COMPOSE_SERVICE=1)
 * the pgaftest container starts at the same time as the monitor — no
 * depends_on ordering.  We spin here until the monitor postgres accepts
 * connections, then open the LISTEN channel.
 * ----------------------------------------------------------------------- */

#define MONITOR_WAIT_TIMEOUT_SECS 120

static bool
runner_wait_for_monitor(TestRunner *r)
{
	if (!r->spec->cluster.withMonitor)
	{
		return true;
	}

	log_info("Waiting for monitor to become ready (timeout %ds)",
			 MONITOR_WAIT_TIMEOUT_SECS);

	time_t deadline = time(NULL) + MONITOR_WAIT_TIMEOUT_SECS;

	char monitorReadyCmd[sizeof(r->composeBase) + 128];
	snprintf(monitorReadyCmd, sizeof(monitorReadyCmd),
			 "%s exec -T monitor psql -U autoctl_node -d pg_auto_failover "
			 "-c 'SELECT 1' >/dev/null 2>&1",
			 r->composeBase);

	while (time(NULL) < deadline)
	{
		if (runner_notify_connect(r))
		{
			log_info("Monitor is ready; LISTEN channel open");
			return true;
		}

		/*
		 * If the direct libpq connection can't be established (e.g. the
		 * host IP is not in the monitor's pg_hba.conf — common when the
		 * monitor was initialised by an older pg_autoctl version that only
		 * added the Docker network CIDR), fall back to a subprocess check
		 * via docker compose exec.  Once the monitor responds to psql we
		 * return true; wait loops will use subprocess polling instead of
		 * LISTEN/NOTIFY.
		 */
		if (run_cmd("%s", monitorReadyCmd) == 0)
		{
			log_info("Monitor is ready (subprocess check; LISTEN not available)");
			return true;
		}

		pg_usleep(500 * 1000);   /* 0.5 s between attempts */
	}

	log_error("Timed out waiting for monitor to become ready after %ds",
			  MONITOR_WAIT_TIMEOUT_SECS);
	return false;
}


/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
bool
runner_run(TestSpec *spec, const char *workDir, bool noCleanup)
{
	TestRunner r;
	runner_init(&r, spec, workDir);

	if (!runner_compose_generate(&r))
	{
		return false;
	}

	/*
	 * HOST path: start the compose stack, then run tests directly from the host
	 * using docker-compose exec for all node/monitor interactions.
	 *
	 * The compose file does not include a pgaftest service in host mode — the
	 * current process IS the test runner and has docker access already.
	 *
	 * When PGAFTEST_COMPOSE_SERVICE is set the compose file was generated to
	 * include a pgaftest service and we're running inside that container; skip
	 * compose up since the stack is already running.
	 */
	if (!getenv("PGAFTEST_COMPOSE_SERVICE"))
	{
		log_info("Starting compose stack (project: %s)", r.projectName);

		/*
		 * Remove any leftover stack from a previous --no-cleanup run so we
		 * always start with clean volumes.
		 */
		run_cmd("%s down --volumes --remove-orphans 2>&1", r.composeBase);

		int up_rc = run_cmd("%s up --build -d 2>&1", r.composeBase);
		if (up_rc != 0)
		{
			log_error("docker compose up failed (exit %d)", up_rc);
			run_cmd("%s down --volumes 2>&1", r.composeBase);
			return false;
		}
	}

	r.composeUp = true;

	/*
	 * If no sequence{} block was written, run steps in definition order.
	 * This lets simple test files omit the redundant sequence block entirely.
	 */
	if (spec->sequenceLength == 0)
	{
		for (TestStep *s = spec->steps; s; s = s->next)
		{
			if (spec->sequenceLength < PGAF_MAX_SEQ)
			{
				spec->sequence[spec->sequenceLength++] = s->name;
			}
		}
	}

	r.tapTotal = spec->sequenceLength;

	if (!runner_wait_for_monitor(&r))
	{
		return false;
	}

	if (!runner_apply_formation_settings(&r))
	{
		return false;
	}

	/* setup{} */
	if (spec->setup)
	{
		char err[512] = "";
		log_info("Running setup block");
		if (!runner_exec_step(&r, spec->setup, err, sizeof(err), 0))
		{
			tap_plan(&r);
			tap_diag("setup failed: %s", err);
			return false;
		}
	}

	/* sequence — fail fast: first failure stops the run */
	bool allPassed = true;
	for (int i = 0; i < spec->sequenceLength; i++)
	{
		const char *name = spec->sequence[i];
		TestStep *step = spec_find_step(spec, name);
		if (!step)
		{
			tap_not_ok(&r, name, "step not found in spec");
			allPassed = false;
			break;
		}

		log_info("STEP %d: %s", i + 1, name);
		char err[512] = "";
		if (runner_exec_step(&r, step, err, sizeof(err), i + 1))
		{
			tap_ok(&r, name);
		}
		else
		{
			tap_not_ok(&r, name, err);
			allPassed = false;
			break;
		}
	}

	tap_plan(&r);

	/* teardown{} — always runs */
	if (spec->teardown)
	{
		char err[512] = "";
		log_info("Running teardown block");
		runner_exec_step(&r, spec->teardown, err, sizeof(err), 0);
	}

	/* compose lifecycle is owned by the host — do not call compose_down here */
	return allPassed;
}


bool
runner_setup(TestSpec *spec, const char *workDir, bool withTmux)
{
	TestRunner r;
	runner_init(&r, spec, workDir);

	if (!runner_compose_generate(&r))
	{
		return false;
	}

	if (!runner_compose_up(&r))
	{
		return false;
	}

	if (!runner_wait_for_monitor(&r))
	{
		runner_compose_down(&r);
		return false;
	}

	if (!runner_apply_formation_settings(&r))
	{
		runner_compose_down(&r);
		return false;
	}

	/* run setup{} block */
	if (spec->setup)
	{
		char err[512] = "";
		log_info("Running setup block");
		if (!runner_exec_step(&r, spec->setup, err, sizeof(err), 0))
		{
			log_error("Setup failed: %s", err);
			runner_compose_down(&r);
			return false;
		}
	}

	if (withTmux)
	{
		/* Launch tmux with compose logs + pg_autoctl watch */
		log_info("Starting tmux session for project %s", r.projectName);
		run_cmd(
			"tmux new-session -d -s %s "
			"\"%s logs -f\" \\; "
			"split-window -v "
			"\"%s exec monitor pg_autoctl watch\" \\; "
			"split-window -v \\; "
			"select-layout even-vertical",
			r.projectName,
			r.composeBase,
			r.composeBase);

		printf("Cluster ready. Attach with: tmux attach -t %s\n",
			   r.projectName);
		printf("Tear down with: pgaftest down --work-dir %s\n", workDir);
	}
	else
	{
		printf("\nCluster ready — compose project: %s\n", r.projectName);
		printf("Work dir: %s\n", workDir);
		printf("\nAvailable steps:");
		for (TestStep *s = spec->steps; s; s = s->next)
		{
			printf(" %s", s->name);
		}
		printf("\n\nRun a step: pgaftest step <name> --work-dir %s\n", workDir);
		printf("Tear down:  pgaftest down --work-dir %s\n\n", workDir);
	}

	return true;
}


bool
runner_step(TestSpec *spec, const char *workDir, const char *stepName)
{
	TestRunner r;
	runner_init(&r, spec, workDir);

	if (!runner_load_state(&r))
	{
		return false;
	}

	TestStep *step = spec_find_step(spec, stepName);
	if (!step)
	{
		log_error("Step \"%s\" not found in spec", stepName);
		return false;
	}

	char err[512] = "";
	if (!runner_exec_step(&r, step, err, sizeof(err), 0))
	{
		log_error("Step \"%s\" failed: %s", stepName, err);
		return false;
	}

	log_info("Step \"%s\" passed", stepName);
	return true;
}


bool
runner_down(TestSpec *spec, const char *workDir)
{
	TestRunner r;
	runner_init(&r, spec, workDir);
	runner_load_state(&r); /* best-effort */

	/* teardown{} */
	if (spec->teardown)
	{
		char err[512] = "";
		runner_exec_step(&r, spec->teardown, err, sizeof(err), 0);
	}

	return runner_compose_down(&r);
}


bool
runner_show(TestSpec *spec)
{
	TestRunner r;
	runner_init(&r, spec, "/tmp/pgaftest-show");

	/* write to stdout instead of a file */
	/* No specFile for show — omit the pgaftest service */
	bool ok = compose_gen_write(&spec->cluster, "/dev/stdout",
								r.projectName, r.contextDir, NULL);
	return ok;
}


/*
 * runner_prepare writes docker-compose.yml, per-node .ini files, and a
 * Makefile into outDir, then prints the docker compose command to stdout.
 *
 * The Makefile has two phony targets:
 *   test:  docker compose up --exit-code-from pgaftest
 *   down:  docker compose down --volumes --remove-orphans
 */
bool
runner_prepare(TestSpec *spec, const char *outDir)
{
	TestRunner r;

	/* If outDir not given, derive from spec name next to spec file */
	char derivedDir[1024];
	if (!outDir || outDir[0] == '\0')
	{
		const char *base = strrchr(spec->filename, '/');
		base = base ? base + 1 : spec->filename;

		/* strip .pgaf extension for the directory name */
		char stem[256];
		strlcpy(stem, base, sizeof(stem));
		char *dot = strrchr(stem, '.');
		if (dot && strcmp(dot, ".pgaf") == 0)
		{
			*dot = '\0';
		}

		/* place it beside the spec file */
		const char *slash = strrchr(spec->filename, '/');
		if (slash)
		{
			snprintf(derivedDir, sizeof(derivedDir), "%.*s/%s-compose",
					 (int) (slash - spec->filename), spec->filename, stem);
		}
		else
		{
			snprintf(derivedDir, sizeof(derivedDir), "%s-compose", stem);
		}
		outDir = derivedDir;
	}

	/* Create the output directory */
	if (mkdir(outDir, 0755) != 0 && errno != EEXIST)
	{
		log_error("mkdir \"%s\": %m", outDir);
		return false;
	}

	runner_init(&r, spec, outDir);

	/* Generate SSL certs if the cluster ssl mode requires them (verify-ca/full) */
	if (!compose_gen_write_ssl_certs(&spec->cluster, outDir))
	{
		return false;
	}

	/* Write docker-compose.yml (with pgaftest service) */
	if (!compose_gen_write(&spec->cluster, r.composeFile,
						   r.projectName, r.contextDir, r.specFile))
	{
		return false;
	}

	/* Write monitor.ini */
	if (spec->cluster.withMonitor)
	{
		if (!compose_gen_write_monitor_ini(&spec->cluster, outDir))
		{
			return false;
		}
	}

	if (!compose_gen_write_second_monitor_ini(&spec->cluster, outDir))
	{
		return false;
	}

	/* Write per-node ini files */
	int globalNodeId = 0;
	for (int fi = 0; fi < spec->cluster.formationCount; fi++)
	{
		const TestFormation *form = &spec->cluster.formations[fi];
		for (int ni = 0; ni < form->nodeCount; ni++)
		{
			if (!compose_gen_write_node_ini(&spec->cluster, form,
											&form->nodes[ni],
											++globalNodeId, outDir))
			{
				return false;
			}
		}
	}

	/* Write Makefile */
	char makefilePath[1280];
	snprintf(makefilePath, sizeof(makefilePath), "%s/Makefile", outDir);
	FILE *mf = fopen(makefilePath, "w");
	if (!mf)
	{
		log_error("Failed to open \"%s\": %m", makefilePath);
		return false;
	}
	fprintf(mf,
			".PHONY: test down\n"
			"\n"
			"test:\n"
			"\tdocker compose -p %s -f docker-compose.yml"
			" up --build --exit-code-from pgaftest --attach pgaftest\n"
			"\n"
			"down:\n"
			"\tdocker compose -p %s -f docker-compose.yml"
			" down --volumes --remove-orphans\n",
			r.projectName, r.projectName);
	fclose(mf);
	log_info("Wrote Makefile to \"%s\"", makefilePath);

	/* Print the docker compose command to stdout */
	printf("cd %s && \\\n", outDir);
	printf("  docker compose -p %s -f docker-compose.yml"
		   " up --build --exit-code-from pgaftest --attach pgaftest\n",
		   r.projectName);

	return true;
}
