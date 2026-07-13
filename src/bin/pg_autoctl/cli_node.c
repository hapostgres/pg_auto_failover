/*
 * src/bin/pg_autoctl/cli_node.c
 *   pg_autoctl node — declarative node lifecycle driven by a pg_autoctl_node.ini file.
 *
 *   pg_autoctl node run   <file>    Create (if needed) then run the supervisor.
 *   pg_autoctl node apply <file>    Converge mutable fields on a running node.
 *   pg_autoctl node show            Dump current config as pg_autoctl_node.ini.
 *   pg_autoctl node check <file>    Validate the file without creating anything.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "cli_common.h"
#include "cli_node.h"
#include "cli_root.h"
#include "commandline.h"
#include "defaults.h"
#include "env_utils.h"
#include "file_utils.h"
#include "keeper_config.h"
#include "log.h"
#include "nodespec.h"
#include "pgsetup.h"
#include "runprogram.h"
#include "string_utils.h"


static int cli_node_run_getopts(int argc, char **argv);
static void cli_node_run(int argc, char **argv);
static int cli_node_init_getopts(int argc, char **argv);
static void cli_node_init(int argc, char **argv);
static int cli_node_apply_getopts(int argc, char **argv);
static void cli_node_apply(int argc, char **argv);
static int cli_node_start_getopts(int argc, char **argv);
static void cli_node_start(int argc, char **argv);
static int cli_node_show_getopts(int argc, char **argv);
static void cli_node_show(int argc, char **argv);
static int cli_node_check_getopts(int argc, char **argv);
static void cli_node_check(int argc, char **argv);


static CommandLine node_run_command =
	make_command(
		"run",
		"Create (if needed) and run a node described by a pg_autoctl_node.ini file",
		"<file.ini>",
		"  <file.ini>   path to the pg_autoctl_node.ini file\n"
		"               (default: " PG_AUTOCTL_NODESPEC_PATH ")\n",
		cli_node_run_getopts,
		cli_node_run);

static CommandLine node_init_command =
	make_command(
		"init",
		"Initialize a node's PGDATA from a pg_autoctl_node.ini file (no --run)",
		"<file.ini>",
		"  <file.ini>   path to the pg_autoctl_node.ini file\n"
		"\n"
		"  Runs `pg_autoctl create <kind> ...` without --run, so that the\n"
		"  Postgres data directory is initialized (and the monitor registered)\n"
		"  without starting the supervisor.  Useful in Dockerfile stages to\n"
		"  pre-bake initdb into an image layer.  For a monitor node, no external\n"
		"  dependencies are needed.  For data nodes the monitor must be reachable.\n",
		cli_node_init_getopts,
		cli_node_init);

static CommandLine node_apply_command =
	make_command(
		"apply",
		"Apply mutable settings from a pg_autoctl_node.ini to a running node",
		"<file.ini>",
		"  <file.ini>   path to the pg_autoctl_node.ini file\n",
		cli_node_apply_getopts,
		cli_node_apply);

static CommandLine node_start_command =
	make_command(
		"start",
		"Start a node waiting in launch=deferred mode (idempotent)",
		"[<file.ini>]",
		"  <file.ini>   path to the pg_autoctl_node.ini file\n"
		"               (default: " PG_AUTOCTL_NODESPEC_PATH ")\n",
		cli_node_start_getopts,
		cli_node_start);

static CommandLine node_show_command =
	make_command(
		"show",
		"Dump current node configuration as a pg_autoctl_node.ini file",
		"[--pgdata <dir>]",
		"  --pgdata <dir>   location of the Postgres data directory\n",
		cli_node_show_getopts,
		cli_node_show);

static CommandLine node_check_command =
	make_command(
		"check",
		"Validate a pg_autoctl_node.ini file without creating anything",
		"<file.ini>",
		"  <file.ini>   path to the pg_autoctl_node.ini file\n",
		cli_node_check_getopts,
		cli_node_check);

static CommandLine *node_subcommands[] = {
	&node_run_command,
	&node_init_command,
	&node_apply_command,
	&node_start_command,
	&node_show_command,
	&node_check_command,
	NULL
};

CommandLine node_commands =
	make_command_set(
		"node",
		"Declarative node lifecycle — create, run, and reconfigure from a .ini file",
		NULL, NULL, NULL, node_subcommands);


/* -----------------------------------------------------------------------
 * Shared state
 * ----------------------------------------------------------------------- */

static char nodeSpecPath[MAXPGPATH] = { 0 };


/* -----------------------------------------------------------------------
 * Shared helpers used by cli_node_run and cli_node_init
 * ----------------------------------------------------------------------- */

/*
 * copy_file copies src to dst using read()/write().  Returns true on success.
 */
static bool
copy_file(const char *src, const char *dst)
{
	int in_fd = open(src, O_RDONLY);
	if (in_fd < 0)
	{
		log_error("Cannot open \"%s\": %m", src);
		return false;
	}

	int out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out_fd < 0)
	{
		log_error("Cannot open \"%s\" for writing: %m", dst);
		close(in_fd);
		return false;
	}

	char buf[4096];
	ssize_t n;
	bool ok = true;

	while ((n = read(in_fd, buf, sizeof(buf))) > 0)
	{
		if (write(out_fd, buf, (size_t) n) != n)
		{
			log_error("Write error on \"%s\": %m", dst);
			ok = false;
			break;
		}
	}
	if (n < 0)
	{
		log_error("Read error on \"%s\": %m", src);
		ok = false;
	}

	close(in_fd);
	close(out_fd);
	return ok;
}


/*
 * node_copy_ssl_certs copies server and client certificates into the
 * locations that PostgreSQL and libpq expect:
 *
 *   server cert/key  → $HOME/{server.crt,server.key}
 *   client cert/key  → $HOME/.postgresql/{postgresql.crt,.key}
 *   CA cert          → $HOME/.postgresql/root.crt
 *
 * Source paths are derived from spec->ssl_ca_file (the directory containing
 * it also has server/ and client/ subdirectories).
 */
static bool
node_copy_ssl_certs(const NodeSpec *spec)
{
	const char *home = getenv("HOME"); /* IGNORE-BANNED */
	if (!home || home[0] == '\0')
	{
		home = "/var/lib/postgres";
	}

	/* Derive SSL root dir from ssl_ca_file, e.g. /etc/pgaf/ssl/ca.crt → /etc/pgaf/ssl */
	char ssl_dir[MAXPGPATH];
	strlcpy(ssl_dir, spec->ssl_ca_file, sizeof(ssl_dir));
	char *last_slash = strrchr(ssl_dir, '/');
	if (last_slash)
	{
		*last_slash = '\0';
	}

	/* Server cert and key → home dir (PostgreSQL reads them there) */
	char dst_crt[MAXPGPATH], dst_key[MAXPGPATH];
	sformat(dst_crt, sizeof(dst_crt), "%s/server.crt", home);
	sformat(dst_key, sizeof(dst_key), "%s/server.key", home);

	if (!copy_file(spec->ssl_cert_file, dst_crt))
	{
		return false;
	}
	if (!copy_file(spec->ssl_key_file, dst_key))
	{
		return false;
	}
	if (chmod(dst_key, 0600) != 0)
	{
		log_error("chmod 0600 \"%s\": %m", dst_key);
		return false;
	}

	/* ~/.postgresql/ for libpq client certs */
	char pg_dir[MAXPGPATH];
	sformat(pg_dir, sizeof(pg_dir), "%s/.postgresql", home);
	if (mkdir(pg_dir, 0700) != 0 && errno != EEXIST)
	{
		log_error("mkdir \"%s\": %m", pg_dir);
		return false;
	}

	char src_client_crt[MAXPGPATH], src_client_key[MAXPGPATH];
	char dst_client_crt[MAXPGPATH], dst_client_key[MAXPGPATH], dst_root[MAXPGPATH];
	sformat(src_client_crt, sizeof(src_client_crt), "%s/client/postgresql.crt", ssl_dir);
	sformat(src_client_key, sizeof(src_client_key), "%s/client/postgresql.key", ssl_dir);
	sformat(dst_client_crt, sizeof(dst_client_crt), "%s/.postgresql/postgresql.crt",
			home);
	sformat(dst_client_key, sizeof(dst_client_key), "%s/.postgresql/postgresql.key",
			home);
	sformat(dst_root, sizeof(dst_root), "%s/.postgresql/root.crt", home);

	if (!copy_file(src_client_crt, dst_client_crt))
	{
		return false;
	}
	if (!copy_file(src_client_key, dst_client_key))
	{
		return false;
	}
	if (chmod(dst_client_key, 0600) != 0)
	{
		log_error("chmod 0600 \"%s\": %m", dst_client_key);
		return false;
	}
	if (!copy_file(spec->ssl_ca_file, dst_root))
	{
		return false;
	}

	log_info("SSL certs copied to %s and %s/.postgresql/", home, home);
	return true;
}


/*
 * log_argv prints an argv[] to the log, masking password arguments.
 */
static void
log_argv(const char *prefix, char **args, int nargs)
{
	PQExpBuffer cmd = createPQExpBuffer();
	static const char *pwFlags[] = {
		"--monitor-password",
		"--replication-password",
		"--autoctl-node-password",
		NULL
	};

	for (int i = 0; i < nargs; i++)
	{
		if (i > 0)
		{
			appendPQExpBufferChar(cmd, ' ');
		}

		bool maskThis = false;
		if (i > 0)
		{
			for (int k = 0; pwFlags[k]; k++)
			{
				if (strcmp(args[i - 1], pwFlags[k]) == 0)
				{
					maskThis = true;
					break;
				}
			}
		}
		appendPQExpBufferStr(cmd, maskThis ? "****" : args[i]);
	}
	log_info("%s: %s", prefix, cmd->data);
	destroyPQExpBuffer(cmd);
}


/*
 * node_do_init runs `pg_autoctl create <kind>` (no --run) and waits for it
 * to finish.  Used both by cli_node_init and the cold-start path of
 * cli_node_run so that both share the same underlying initialisation logic.
 *
 * Returns true on success (exit code 0), false otherwise.
 */
static bool
node_do_init(const NodeSpec *spec)
{
	char *args[40];
	int nargs = nodespec_create_argv(spec, pg_autoctl_program, args, 32);
	if (nargs < 0)
	{
		return false;
	}

	/* nodespec_create_argv always appends --run; strip it */
	if (nargs >= 2 && strcmp(args[nargs - 1], "--run") == 0)
	{
		args[nargs - 1] = NULL;
		nargs--;
	}

	log_argv("pg_autoctl node init", args, nargs);

	pid_t pid = fork();
	if (pid < 0)
	{
		log_fatal("fork: %m");
		return false;
	}

	if (pid == 0)
	{
		execv(args[0], args);
		_exit(127);
	}

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
	{ }

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		log_error("pg_autoctl create exited with status %d",
				  WIFEXITED(status) ? WEXITSTATUS(status) : -1);
		return false;
	}

	return true;
}


/* -----------------------------------------------------------------------
 * pg_autoctl node run <file>
 * ----------------------------------------------------------------------- */
static int
cli_node_run_getopts(int argc, char **argv)
{
	/* argv[0] is the subcommand name ("run"/"apply"/"check"); the optional
	 * file path is the first remaining positional argument at argv[1]. */
	if (argc > 1 && argv[1][0] != '-')
	{
		strlcpy(nodeSpecPath, argv[1], sizeof(nodeSpecPath));
	}
	else
	{
		strlcpy(nodeSpecPath, PG_AUTOCTL_NODESPEC_PATH, sizeof(nodeSpecPath));
	}

	return 0;
}


/*
 * cli_node_run reads the pg_autoctl_node.ini file, runs `pg_autoctl create
 * <kind> --run` if PGDATA does not yet exist, or `pg_autoctl run` if it does.
 *
 * We exec() rather than call the internal functions directly so that:
 *  1. The supervisor's fork+exec live-upgrade pattern is preserved.
 *  2. The already-running supervisor (PID 1) can restart this process after
 *     a binary upgrade without special-casing the "node run" path.
 */
static void
cli_node_run(int argc, char **argv)
{
	NodeSpec spec = { 0 };

	if (!nodespec_read(nodeSpecPath, &spec))
	{
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/*
	 * [launch] mode = deferred: spin here re-reading nodeSpecPath until
	 * pg_autoctl node start rewrites it with mode = immediate.
	 */
	if (spec.launchDeferred)
	{
		log_info("Node configured with launch = deferred in \"%s\"; "
				 "waiting for pg_autoctl node start", nodeSpecPath);

		for (;;)
		{
			pg_usleep(500 * 1000);   /* 0.5 s poll */

			NodeSpec polled = { 0 };
			if (nodespec_read(nodeSpecPath, &polled) && !polled.launchDeferred)
			{
				spec = polled;
				break;
			}
		}

		log_info("launch = immediate; proceeding with node initialization");
	}

	/*
	 * Delete any leftover PID file from a previous run.  This is safe here
	 * because we have not started a supervisor yet.  Stale PID files cause
	 * pg_autoctl to refuse to start, so remove them unconditionally.
	 */
	if (!IS_EMPTY_STRING_BUFFER(spec.pgdata))
	{
		char pidPath[MAXPGPATH];
		sformat(pidPath, sizeof(pidPath),
				"/tmp/pg_autoctl%s/pg_autoctl.pid", spec.pgdata);
		(void) unlink(pidPath);   /* ignore ENOENT */
	}

	/*
	 * CA-signed SSL: copy server and client certs into the locations that
	 * PostgreSQL and libpq expect.  This must happen before pg_autoctl
	 * create (which configures SSL) and before pg_autoctl run.
	 */
	if (!IS_EMPTY_STRING_BUFFER(spec.ssl_ca_file))
	{
		if (!node_copy_ssl_certs(&spec))
		{
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}

	/*
	 * Tell the supervisor which spec file to watch for live changes.
	 * Set before either execv so the child inherits it.
	 */
	setenv("PG_AUTOCTL_NODESPEC", nodeSpecPath, 1);

	/*
	 * Check whether PGDATA already has pg_autoctl.cfg.
	 */
	char cfgPath[MAXPGPATH];
	bool cfgExists = false;

	if (!IS_EMPTY_STRING_BUFFER(spec.pgdata))
	{
		sformat(cfgPath, sizeof(cfgPath), "%s/pg_autoctl.cfg", spec.pgdata);
		cfgExists = file_exists(cfgPath);
	}

	if (!cfgExists)
	{
		/*
		 * Cold start: the node has never been initialized.
		 *
		 * If debian_cluster is set, run pg_createcluster first so that
		 * pg_autoctl create detects the Debian-style split-config layout.
		 */
		if (!IS_EMPTY_STRING_BUFFER(spec.debianCluster))
		{
			/* Derive PG major version from path /var/lib/postgresql/<ver>/... */
			int pgmajor = 0;
			const char pfx[] = "/var/lib/postgresql/";
			if (strncmp(spec.pgdata, pfx, strlen(pfx)) == 0)
			{
				pgmajor = atoi(spec.pgdata + strlen(pfx)); /* IGNORE-BANNED */
			}
			if (pgmajor <= 0)
			{
				log_error("Cannot determine PG major version from pgdata \"%s\"",
						  spec.pgdata);
				exit(EXIT_CODE_BAD_CONFIG);
			}

			char pgmajor_str[8];
			sformat(pgmajor_str, sizeof(pgmajor_str), "%d", pgmajor);

			char *pgcc_args[] = {
				"pg_createcluster",
				"--user", "docker",
				"--group", "postgres",
				pgmajor_str,
				spec.debianCluster,
				"--",
				"--auth-local", "trust",
				"--auth-host", "trust",
				NULL
			};
			log_info("pg_autoctl node run: pg_createcluster %d %s",
					 pgmajor, spec.debianCluster);

			pid_t pid = fork();
			if (pid < 0)
			{
				log_fatal("fork: %m");
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
			if (pid == 0)
			{
				execvp("pg_createcluster", pgcc_args);
				_exit(127);
			}
			int st = 0;
			while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
			{ }
			if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
			{
				log_error("pg_createcluster exited with status %d",
						  WIFEXITED(st) ? WEXITSTATUS(st) : -1);
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
		}

		/*
		 * PG_AUTOCTL_TEST_DELAY: stagger node registration so that node IDs
		 * are assigned in name order (node1 → id 1, node2 → id 2, …).
		 */
		if (env_exists("PG_AUTOCTL_TEST_DELAY") && spec.name[0] != '\0')
		{
			const char *p = spec.name + strlen(spec.name);
			while (p > spec.name && isdigit((unsigned char) p[-1]))
			{
				p--;
			}
			if (*p != '\0')
			{
				int n = atoi(p); /* IGNORE-BANNED */
				int secs = 2 * n;
				log_info("PG_AUTOCTL_TEST_DELAY: sleeping %ds before "
						 "registration (node %s, index %d)",
						 secs, spec.name, n);
				sleep(secs);
			}
		}

		if (!node_do_init(&spec))
		{
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
	else
	{
		/*
		 * Warm start: PGDATA already has pg_autoctl.cfg.  Apply any mutable
		 * changes that might have been made to the spec file since the last run.
		 */
		NodeSpec prev = { 0 };
		(void) nodespec_read(nodeSpecPath, &prev);
		(void) nodespec_apply(&spec, &prev);
	}

	/*
	 * Both paths end here: exec `pg_autoctl run --pgdata <dir>`.
	 */
	char *run_args[] = {
		(char *) pg_autoctl_program,
		"run",
		"--pgdata",
		spec.pgdata,
		NULL
	};
	log_argv("pg_autoctl node run", run_args, 4);
	execv(run_args[0], run_args);

	log_fatal("execv(\"%s\"): %m", run_args[0]);
	exit(EXIT_CODE_INTERNAL_ERROR);
}


/* -----------------------------------------------------------------------
 * pg_autoctl node init <file>
 *
 * Like node run, but runs `pg_autoctl create <kind> ...` without --run.
 * The node's PGDATA is initialized (and the monitor is registered) without
 * starting the supervisor.  Intended for Dockerfile stages that pre-bake
 * initdb into a named image layer so that test startup skips the slow
 * initdb + registration step.
 *
 * For the monitor node this is fully self-contained.  For data nodes the
 * monitor must be reachable during the init step.
 * ----------------------------------------------------------------------- */
static int
cli_node_init_getopts(int argc, char **argv)
{
	if (argc > 1 && argv[1][0] != '-')
	{
		strlcpy(nodeSpecPath, argv[1], sizeof(nodeSpecPath));
	}
	else
	{
		strlcpy(nodeSpecPath, PG_AUTOCTL_NODESPEC_PATH, sizeof(nodeSpecPath));
	}

	return 0;
}


static void
cli_node_init(int argc, char **argv)
{
	NodeSpec spec = { 0 };

	if (!nodespec_read(nodeSpecPath, &spec))
	{
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* Idempotent: if PGDATA already initialized, nothing to do. */
	if (!IS_EMPTY_STRING_BUFFER(spec.pgdata))
	{
		char cfgPath[MAXPGPATH];
		sformat(cfgPath, sizeof(cfgPath), "%s/pg_autoctl.cfg", spec.pgdata);
		if (file_exists(cfgPath))
		{
			log_info("Node already initialized at \"%s\"; nothing to do", spec.pgdata);
			exit(0);
		}
	}

	/* PID file cleanup and SSL cert copy follow the same sequence as node run. */
	if (!IS_EMPTY_STRING_BUFFER(spec.pgdata))
	{
		char pidPath[MAXPGPATH];
		sformat(pidPath, sizeof(pidPath),
				"/tmp/pg_autoctl%s/pg_autoctl.pid", spec.pgdata);
		(void) unlink(pidPath);
	}

	if (!IS_EMPTY_STRING_BUFFER(spec.ssl_ca_file))
	{
		if (!node_copy_ssl_certs(&spec))
		{
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}

	if (!node_do_init(&spec))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/* -----------------------------------------------------------------------
 * pg_autoctl node apply <file>
 * ----------------------------------------------------------------------- */
static int
cli_node_apply_getopts(int argc, char **argv)
{
	if (argc > 0 && argv[0][0] != '-')
	{
		strlcpy(nodeSpecPath, argv[0], sizeof(nodeSpecPath));
	}
	else
	{
		log_error("pg_autoctl node apply requires a file argument");
		exit(EXIT_CODE_BAD_ARGS);
	}
	return 0;
}


static void
cli_node_apply(int argc, char **argv)
{
	NodeSpec new_spec = { 0 };
	NodeSpec cur_spec = { 0 };

	if (!nodespec_read(nodeSpecPath, &new_spec))
	{
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* Read the current spec from the same path as a baseline */
	(void) nodespec_read(nodeSpecPath, &cur_spec);

	if (!nodespec_apply(&new_spec, &cur_spec))
	{
		log_error("Failed to apply node spec from \"%s\"", nodeSpecPath);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/* -----------------------------------------------------------------------
 * pg_autoctl node start [<file>]
 *
 * Clears launch = deferred in the spec file so a waiting pg_autoctl node run
 * proceeds.  Idempotent: if the node is already immediate, exits 0 quietly.
 * ----------------------------------------------------------------------- */
static int
cli_node_start_getopts(int argc, char **argv)
{
	if (argc > 1 && argv[1][0] != '-')
	{
		strlcpy(nodeSpecPath, argv[1], sizeof(nodeSpecPath));
	}
	else
	{
		strlcpy(nodeSpecPath, PG_AUTOCTL_NODESPEC_PATH, sizeof(nodeSpecPath));
	}

	return 0;
}


static void
cli_node_start(int argc, char **argv)
{
	NodeSpec spec = { 0 };

	if (!nodespec_read(nodeSpecPath, &spec))
	{
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!spec.launchDeferred)
	{
		log_info("Node \"%s\" launch is already immediate; nothing to do",
				 nodeSpecPath);
		exit(0);
	}

	spec.launchDeferred = false;

	if (!nodespec_write_to_path(&spec, nodeSpecPath))
	{
		log_error("Failed to update \"%s\"", nodeSpecPath);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	log_info("Cleared launch = deferred in \"%s\"; node will now start",
			 nodeSpecPath);
}


/* -----------------------------------------------------------------------
 * pg_autoctl node show [--pgdata <dir>]
 * ----------------------------------------------------------------------- */
static int
cli_node_show_getopts(int argc, char **argv)
{
	/* honour --pgdata from the global keeperOptions */
	return cli_getopt_pgdata(argc, argv);
}


static void
cli_node_show(int argc, char **argv)
{
	/*
	 * Build a NodeSpec from the running keeper/monitor config and emit it.
	 * We read the existing pg_autoctl.cfg rather than the node spec file so
	 * that `pg_autoctl node show` reflects the live configuration.
	 */
	KeeperConfig config = keeperOptions;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	if (!keeper_config_read_file(&config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		log_error("Failed to read pg_autoctl configuration");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	NodeSpec spec = { 0 };

	spec.kind = config.pgSetup.pgKind;
	strlcpy(spec.pgdata, config.pgSetup.pgdata, sizeof(spec.pgdata));
	strlcpy(spec.hostname, config.hostname, sizeof(spec.hostname));
	spec.port = config.pgSetup.pgport;
	strlcpy(spec.monitor_pguri, config.monitor_pguri,
			sizeof(spec.monitor_pguri));
	strlcpy(spec.formation, config.formation, sizeof(spec.formation));
	spec.group = config.groupId;

	/* Mutable settings defaults — we'd need a monitor round-trip for live values */
	spec.candidate_priority = 50;
	spec.replication_quorum = true;

	/* Create-time defaults */
	strlcpy(spec.ssl, "self-signed", sizeof(spec.ssl));
	strlcpy(spec.auth, "trust", sizeof(spec.auth));
	spec.pg_hba_lan = true;

	(void) nodespec_write(&spec, stdout);
}


/* -----------------------------------------------------------------------
 * pg_autoctl node check <file>
 * ----------------------------------------------------------------------- */
static int
cli_node_check_getopts(int argc, char **argv)
{
	if (argc > 0 && argv[0][0] != '-')
	{
		strlcpy(nodeSpecPath, argv[0], sizeof(nodeSpecPath));
	}
	else
	{
		log_error("pg_autoctl node check requires a file argument");
		exit(EXIT_CODE_BAD_ARGS);
	}
	return 0;
}


static void
cli_node_check(int argc, char **argv)
{
	NodeSpec spec = { 0 };

	if (!nodespec_read(nodeSpecPath, &spec))
	{
		log_error("Invalid node spec file \"%s\"", nodeSpecPath);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	const char *kindStr;
	switch (spec.kind)
	{
		case NODE_KIND_UNKNOWN:
		{
			kindStr = "monitor";
			break;
		}

		case NODE_KIND_STANDALONE:
		{
			kindStr = "postgres";
			break;
		}

		case NODE_KIND_CITUS_COORDINATOR:
		{
			kindStr = "coordinator";
			break;
		}

		case NODE_KIND_CITUS_WORKER:
		{
			kindStr = "worker";
			break;
		}

		default:
		{
			kindStr = "unknown";
			break;
		}
	}

	fformat(stdout, "Node spec \"%s\" is valid.\n", nodeSpecPath);
	fformat(stdout, "  kind               : %s\n", kindStr);
	fformat(stdout, "  pgdata             : %s\n", spec.pgdata);
	fformat(stdout, "  hostname           : %s\n", spec.hostname);
	fformat(stdout, "  port               : %d\n", spec.port);

	if (spec.kind != NODE_KIND_UNKNOWN)
	{
		fformat(stdout, "  monitor_pguri      : %s\n", spec.monitor_pguri);
		fformat(stdout, "  formation          : %s\n", spec.formation);
		fformat(stdout, "  group              : %d\n", spec.group);
	}

	fformat(stdout, "  candidate_priority : %d\n", spec.candidate_priority);
	fformat(stdout, "  replication_quorum : %s\n",
			spec.replication_quorum ? "true" : "false");
	fformat(stdout, "  ssl                : %s\n", spec.ssl);
	fformat(stdout, "  auth               : %s\n", spec.auth);
	fformat(stdout, "  pg_hba_lan         : %s\n",
			spec.pg_hba_lan ? "true" : "false");
}
