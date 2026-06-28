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
#include <sys/stat.h>

/* postgres_fe.h must precede pqexpbuffer.h to define pg_attribute_printf and bool */
#include "postgres_fe.h"
#include "libpq-fe.h"
#include "pqexpbuffer.h"

#include "log.h"
#include "compose_gen.h"
#include "test_runner.h"
#include "test_spec.h"

/* Default host port for the monitor */
#define MONITOR_DEFAULT_PORT 15432

/* Docker binary name */
#define DOCKER "docker"

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

/* Run a shell command, return its exit code */
static int
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
static int
run_cmd_capture(char *buf, int buflen, const char *fmt, ...)
{
	char cmd[4096];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(cmd, sizeof(cmd), fmt, ap);
	va_end(ap);

	log_debug("$ %s", cmd);

	FILE *p = popen(cmd, "r");
	if (!p) return -1;

	int pos = 0;
	int c;
	while ((c = fgetc(p)) != EOF && pos < buflen - 1)
		buf[pos++] = (char)c;
	buf[pos] = '\0';

	/* trim trailing whitespace */
	while (pos > 0 && (buf[pos-1] == '\n' || buf[pos-1] == '\r' ||
	       buf[pos-1] == ' '))
		buf[--pos] = '\0';

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
	r->monitorPort = MONITOR_DEFAULT_PORT;

	/* derive project name from spec filename base */
	const char *base = strrchr(spec->filename, '/');
	base = base ? base + 1 : spec->filename;
	strncpy(r->projectName, base, sizeof(r->projectName)-1);
	/* strip .pgaf extension */
	char *dot = strrchr(r->projectName, '.');
	if (dot) *dot = '\0';

	snprintf(r->workDir, sizeof(r->workDir), "%s", workDir);
	snprintf(r->composeFile, sizeof(r->composeFile),
	         "%s/docker-compose.yml", workDir);

	/* build context = directory from which pgaftest was invoked */
	if (getcwd(r->contextDir, sizeof(r->contextDir)) == NULL)
	{
		log_error("getcwd failed: %m");
		r->contextDir[0] = '\0';
	}

	snprintf(r->monitorConnStr, sizeof(r->monitorConnStr),
	         "host=127.0.0.1 port=%d user=autoctl_node "
	         "dbname=pg_auto_failover sslmode=prefer",
	         r->monitorPort);
}

/* -----------------------------------------------------------------------
 * TAP output
 * ----------------------------------------------------------------------- */

static void
tap_plan(TestRunner *r)
{
	printf("1..%d\n", r->tapTotal);
	fflush(stdout);
}

static void
tap_ok(TestRunner *r, const char *name)
{
	r->tapPass++;
	printf("ok %d - %s\n", r->tapPass + r->tapFail, name);
	fflush(stdout);
}

static void
tap_not_ok(TestRunner *r, const char *name, const char *reason)
{
	r->tapFail++;
	printf("not ok %d - %s\n", r->tapPass + r->tapFail, name);
	if (reason)
		printf("# %s\n", reason);
	fflush(stdout);
}

static void
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
	/* ensure workdir exists */
	if (mkdir(r->workDir, 0755) != 0 && errno != EEXIST)
	{
		log_error("Failed to create work directory \"%s\": %m", r->workDir);
		return false;
	}

	if (!compose_gen_write(&r->spec->cluster,
	                       r->composeFile,
	                       r->projectName,
	                       r->monitorPort,
	                       r->contextDir))
		return false;

	/*
	 * Write per-node pg_autoctl_node.ini files into the workdir.  Each one
	 * is bind-mounted into its container at /etc/pgaf/node.ini so that
	 * every container uses the same image and command regardless of role.
	 */
	if (!compose_gen_write_monitor_ini(&r->spec->cluster, r->workDir))
		return false;

	for (int fi = 0; fi < r->spec->cluster.formationCount; fi++)
	{
		const TestFormation *form = &r->spec->cluster.formations[fi];

		for (int ni = 0; ni < form->nodeCount; ni++)
		{
			if (!compose_gen_write_node_ini(&r->spec->cluster, form,
			                                &form->nodes[ni], r->workDir))
				return false;
		}
	}

	return true;
}

static bool
runner_compose_up(TestRunner *r)
{
	log_info("Starting compose stack (project: %s)", r->projectName);

	int rc = run_cmd("docker compose -p %s -f %s up -d 2>&1",
	                 r->projectName, r->composeFile);
	if (rc != 0)
	{
		log_error("docker compose up failed (exit %d)", rc);
		return false;
	}
	r->composeUp = true;
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
			continue;

		log_info("Setting formation \"%s\" number-sync-standbys = %d",
		         form->name, form->numSync);

		int rc = run_cmd(
			"docker compose -p %s -f %s exec monitor "
			"pg_autoctl set formation number-sync-standbys %d "
			"--pgdata /var/lib/postgres/pgaf --formation %s 2>&1",
			r->projectName, r->composeFile,
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
	if (!r->composeUp) return true;

	log_info("Tearing down compose stack (project: %s)", r->projectName);

	run_cmd("docker compose -p %s -f %s down --volumes --remove-orphans 2>&1",
	        r->projectName, r->composeFile);
	r->composeUp = false;
	return true;
}

/* -----------------------------------------------------------------------
 * State polling via libpq
 * ----------------------------------------------------------------------- */

/*
 * Query the monitor for a node's current reported state and assigned state.
 * Returns true and fills *reported / *assigned on success.
 */
static bool
monitor_get_node_state(const char *connstr, const char *nodeName,
                       char *reported, int replen,
                       char *assigned, int asslen)
{
	PGconn *conn = PQconnectdb(connstr);
	if (PQstatus(conn) != CONNECTION_OK)
	{
		log_debug("monitor not yet reachable: %s", PQerrorMessage(conn));
		PQfinish(conn);
		return false;
	}

	const char *params[1] = { nodeName };
	PGresult *res = PQexecParams(conn,
		"SELECT reportedstate, goalstate "
		"FROM pgautofailover.node WHERE nodename = $1",
		1, NULL, params, NULL, NULL, 0);

	bool ok = false;
	if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0)
	{
		strncpy(reported, PQgetvalue(res, 0, 0), replen - 1);
		strncpy(assigned, PQgetvalue(res, 0, 1), asslen - 1);
		ok = true;
	}
	PQclear(res);
	PQfinish(conn);
	return ok;
}

/*
 * Poll until the node reaches the target state, or timeout expires.
 */
static bool
wait_for_state(TestRunner *r, const char *nodeName, const char *targetState,
               int timeoutSecs, bool checkAssigned)
{
	time_t deadline = time(NULL) + timeoutSecs;
	char reported[64], assigned[64];

	log_info("Waiting for %s %s = %s (timeout %ds)",
	         nodeName,
	         checkAssigned ? "assigned-state" : "state",
	         targetState,
	         timeoutSecs);

	while (time(NULL) < deadline)
	{
		if (monitor_get_node_state(r->monitorConnStr, nodeName,
		                           reported, sizeof(reported),
		                           assigned, sizeof(assigned)))
		{
			const char *actual = checkAssigned ? assigned : reported;
			if (strcmp(actual, targetState) == 0)
				return true;
		}
		usleep(200000); /* 200ms */
	}

	log_error("Timeout waiting for %s %s = %s",
	          nodeName,
	          checkAssigned ? "assigned-state" : "state",
	          targetState);
	return false;
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
			if (oi < outlen - 1) out[oi++] = '\'';
		}
		else
			out[oi++] = sql[i];
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
		if (!p) break;
		p += 8;  /* skip "ERROR:  " */
		/* SQLSTATE: exactly 5 alphanumeric characters followed by ':' */
		if (strlen(p) >= 6 && p[5] == ':')
		{
			bool valid = true;
			for (int i = 0; i < 5 && valid; i++)
				valid = isalnum((unsigned char)p[i]);
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
	 */
	int rc = run_cmd_capture(outbuf, outlen,
		"docker compose -p %s -f %s exec -T %s "
		"psql --tuples-only --no-align "
		"-v ON_ERROR_STOP=1 -v VERBOSITY=verbose "
		"-c '%s' 2>&1",
		r->projectName, r->composeFile, service, escaped);

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

	log_info("Disconnecting %s from network %s", container, netName);
	return run_cmd("docker network disconnect %s %s 2>&1",
	               netName, container) == 0;
}

static bool
runner_network_on(TestRunner *r, const char *nodeName)
{
	char netName[128], container[128];
	compose_network_name(r->projectName, netName, sizeof(netName));
	compose_container_name(r->projectName, nodeName, container, sizeof(container));

	log_info("Reconnecting %s to network %s", container, netName);
	return run_cmd("docker network connect %s %s 2>&1",
	               netName, container) == 0;
}

/* -----------------------------------------------------------------------
 * Execute a single command
 * ----------------------------------------------------------------------- */

static bool
runner_exec_cmd(TestRunner *r, TestCmd *cmd, char *errBuf, int errLen)
{
	switch (cmd->kind)
	{
		case CMD_EXEC:
		{
			int rc = run_cmd(
				"docker compose -p %s -f %s exec -T %s %s 2>&1",
				r->projectName, r->composeFile,
				cmd->service, cmd->args);
			if (rc != 0)
			{
				snprintf(errBuf, errLen,
				         "exec %s %s failed (exit %d)",
				         cmd->service, cmd->args, rc);
				return false;
			}
			return true;
		}

		case CMD_EXEC_FAILS:
		{
			int rc = run_cmd(
				"docker compose -p %s -f %s exec -T %s %s 2>&1",
				r->projectName, r->composeFile,
				cmd->service, cmd->args);
			if (rc == 0)
			{
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
			if (!wait_for_state(r, cmd->service, cmd->state,
			                    cmd->timeoutSeconds, false))
			{
				snprintf(errBuf, errLen,
				         "timeout: %s state never reached %s",
				         cmd->service, cmd->state);
				return false;
			}
			return true;

		case CMD_ASSERT_STATE:
		case CMD_ASSERT_ASSIGNED:
		{
			char reported[64] = "", assigned[64] = "";
			if (!monitor_get_node_state(r->monitorConnStr, cmd->service,
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
			if (strcmp(r->lastSqlOutput, cmd->expected) != 0)
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
			if (!runner_network_off(r, cmd->service))
			{
				snprintf(errBuf, errLen,
				         "network disconnect %s failed", cmd->service);
				return false;
			}
			return true;

		case CMD_NETWORK_ON:
			if (!runner_network_on(r, cmd->service))
			{
				snprintf(errBuf, errLen,
				         "network connect %s failed", cmd->service);
				return false;
			}
			return true;

		case CMD_SLEEP:
			log_info("Sleeping %d seconds", cmd->timeoutSeconds);
			sleep(cmd->timeoutSeconds);
			return true;

		case CMD_COMPOSE_DOWN:
			return runner_compose_down(r);
	}
	return true;
}

/* -----------------------------------------------------------------------
 * Execute a step (list of commands)
 * ----------------------------------------------------------------------- */

static bool
runner_exec_step(TestRunner *r, TestStep *step, char *errBuf, int errLen)
{
	for (TestCmd *cmd = step->commands; cmd; cmd = cmd->next)
	{
		if (!runner_exec_cmd(r, cmd, errBuf, errLen))
			return false;
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
 * Public API
 * ----------------------------------------------------------------------- */

bool
runner_run(TestSpec *spec, const char *workDir)
{
	TestRunner r;
	runner_init(&r, spec, workDir);

	r.tapTotal = spec->sequenceLength;

	if (!runner_compose_generate(&r))
		return false;

	if (!runner_compose_up(&r))
		return false;

	if (!runner_apply_formation_settings(&r))
	{
		runner_compose_down(&r);
		return false;
	}

	tap_plan(&r);

	/* setup{} */
	if (spec->setup)
	{
		char err[512] = "";
		log_info("Running setup block");
		if (!runner_exec_step(&r, spec->setup, err, sizeof(err)))
		{
			tap_diag("setup failed: %s", err);
			runner_compose_down(&r);
			return false;
		}
	}

	/* sequence */
	bool allPassed = true;
	for (int i = 0; i < spec->sequenceLength; i++)
	{
		const char *name = spec->sequence[i];
		TestStep *step = spec_find_step(spec, name);
		if (!step)
		{
			tap_not_ok(&r, name, "step not found in spec");
			allPassed = false;
			continue;
		}

		char err[512] = "";
		if (runner_exec_step(&r, step, err, sizeof(err)))
		{
			tap_ok(&r, name);
		}
		else
		{
			tap_not_ok(&r, name, err);
			allPassed = false;
			/* continue running remaining steps */
		}
	}

	/* teardown{} — always runs */
	if (spec->teardown)
	{
		char err[512] = "";
		log_info("Running teardown block");
		runner_exec_step(&r, spec->teardown, err, sizeof(err));
	}

	runner_compose_down(&r);
	return allPassed;
}

bool
runner_setup(TestSpec *spec, const char *workDir, bool withTmux)
{
	TestRunner r;
	runner_init(&r, spec, workDir);

	if (!runner_compose_generate(&r))
		return false;

	if (!runner_compose_up(&r))
		return false;

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
		if (!runner_exec_step(&r, spec->setup, err, sizeof(err)))
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
			"\"docker compose -p %s -f %s logs -f\" \\; "
			"split-window -v "
			"\"docker compose -p %s -f %s exec monitor pg_autoctl watch\" \\; "
			"split-window -v \\; "
			"select-layout even-vertical",
			r.projectName,
			r.projectName, r.composeFile,
			r.projectName, r.composeFile);

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
			printf(" %s", s->name);
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
		return false;

	TestStep *step = spec_find_step(spec, stepName);
	if (!step)
	{
		log_error("Step \"%s\" not found in spec", stepName);
		return false;
	}

	char err[512] = "";
	if (!runner_exec_step(&r, step, err, sizeof(err)))
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
		runner_exec_step(&r, spec->teardown, err, sizeof(err));
	}

	return runner_compose_down(&r);
}

bool
runner_show(TestSpec *spec)
{
	TestRunner r;
	runner_init(&r, spec, "/tmp/pgaftest-show");

	/* write to stdout instead of a file */
	bool ok = compose_gen_write(&spec->cluster, "/dev/stdout",
	                            r.projectName, r.monitorPort, r.contextDir);
	return ok;
}
