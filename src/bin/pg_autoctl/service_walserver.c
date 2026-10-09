/*
 * src/bin/pg_autoctl/service_walserver.c
 *   Create, read/write, and run a pg_autoctl "walserver" node's own
 *   configuration and supervised `pg_walserver serve` child process.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "config.h"
#include "defaults.h"
#include "file_utils.h"
#include "ini_file.h"
#include "log.h"
#include "pgsetup.h"
#include "pidfile.h"
#include "service_walserver.h"
#include "string_utils.h"
#include "supervisor.h"


/*
 * walserver_config_write writes config->pathnames.config as the minimal
 * configuration file shape for a walserver node:
 *
 *   [pg_autoctl]
 *   role = walserver
 *
 *   [walserver]
 *   pgdata = <config->pgdata>
 *   port   = <config->port>        (only when non-zero)
 *
 * ProbeConfigurationFileRole() recognises this file solely from the
 * [pg_autoctl] role=walserver line, exactly like it already recognises
 * role=monitor / role=keeper for the other node kinds.
 */
bool
walserver_config_write(WalServerConfig *config)
{
	FILE *out = fopen(config->pathnames.config, "w"); /* IGNORE-BANNED */

	if (out == NULL)
	{
		log_error("Failed to open \"%s\" for writing: %m",
				  config->pathnames.config);
		return false;
	}

	fformat(out,
			"[pg_autoctl]\n"
			"role = %s\n"
			"\n"
			"[walserver]\n"
			"pgdata = %s\n",
			WALSERVER_ROLE,
			config->pgdata);

	if (config->port > 0)
	{
		fformat(out, "port = %d\n", config->port);
	}

	if (!IS_EMPTY_STRING_BUFFER(config->name))
	{
		fformat(out, "name = %s\n", config->name);
	}

	fclose(out); /* IGNORE-BANNED */

	return true;
}


/*
 * walserver_config_read parses a walserver node's own configuration file (as
 * written by walserver_config_write) into *config. The caller is expected to
 * have already set config->pathnames (e.g. via
 * keeper_config_set_pathnames_from_pgdata()) before calling this function,
 * unless path is used to discover pgdata instead.
 */
bool
walserver_config_read(const char *path, WalServerConfig *config)
{
	int port = 0;

	IniOption opts[] = {
		make_strbuf_option("walserver", "pgdata", NULL, true,
						   sizeof(config->pgdata), config->pgdata),
		make_int_option_default("walserver", "port", NULL, false, &port, 0),
		make_strbuf_option_default("walserver", "name", NULL, false,
								   sizeof(config->name), config->name, ""),
		INI_OPTION_LAST
	};

	if (!read_ini_file(path, opts))
	{
		log_error("Failed to parse walserver configuration file \"%s\"", path);
		return false;
	}

	config->port = port;

	return true;
}


/*
 * service_walserver_ctl_start forks and execvp()s `pg_walserver serve` as
 * the supervised child process for a walserver node. Modelled directly on
 * service_postgres_ctl_start(), but much simpler: there is no "pg_autoctl
 * internal service" re-exec indirection here, we go straight to the real
 * binary, found on PATH exactly like the Dockerfile installs it (next to
 * pg_autoctl itself, see Dockerfile's run-stage COPY rules).
 */
bool
service_walserver_ctl_start(void *context, pid_t *pid)
{
	WalServerConfig *config = (WalServerConfig *) context;

	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	pid_t fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the pg_walserver controller process: %m");
			return false;
		}

		case 0:
		{
			char portBuffer[16] = { 0 };

			char *args[8];
			int argsIndex = 0;

			args[argsIndex++] = "pg_walserver";
			args[argsIndex++] = "serve";
			args[argsIndex++] = "--pgdata";
			args[argsIndex++] = config->pgdata;

			if (config->port > 0)
			{
				sformat(portBuffer, sizeof(portBuffer), "%d", config->port);
				args[argsIndex++] = "--port";
				args[argsIndex++] = portBuffer;
			}

			args[argsIndex] = NULL;

			execvp(args[0], args);

			/* unexpected: execvp() only returns on failure */
			log_fatal("execvp(\"pg_walserver\"): %m");
			_exit(127);
		}

		default:
		{
			log_debug("pg_autoctl started pg_walserver in subprocess %d", fpid);
			*pid = fpid;

			return true;
		}
	}
}


/*
 * start_walserver registers the single `pg_walserver serve` service with the
 * supervisor and starts it. Unlike start_monitor()/start_keeper(), there is
 * no keeper-init/activation dance: a walserver node never registers with a
 * monitor and never participates in the keeper FSM, so a single permanent
 * service is all there is to supervise.
 */
bool
start_walserver(WalServerConfig *config)
{
	Service subprocesses[] = {
		{
			SERVICE_NAME_WALSERVER,
			RP_PERMANENT,
			-1,
			&service_walserver_ctl_start,
			(void *) config
		}
	};

	int subprocessesCount = sizeof(subprocesses) / sizeof(subprocesses[0]);

	return supervisor_start(subprocesses, subprocessesCount,
							config->pathnames.pid);
}
