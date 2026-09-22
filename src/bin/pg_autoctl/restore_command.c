/*
 * src/bin/pg_autoctl/restore_command.c
 *   See restore_command.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <unistd.h>

#include "cli_root.h"
#include "config.h"
#include "defaults.h"
#include "file_utils.h"
#include "keeper_config.h"
#include "log.h"
#include "monitor.h"
#include "pgsql.h"
#include "restore_command.h"
#include "string_utils.h"

/* pgdata-relative cache file, same shape as ARCHIVE_CONFIRM_CACHE_FILENAME */
#define RESTORE_COMMAND_CACHE_FILENAME "pg_autoctl.restore-command"

/* single-attempt monitor timeout: Postgres is our own retry loop */
#define RESTORE_COMMAND_TIMEOUT 3


/*
 * restore_command_cache_path builds the cache file's full path at pgdata.
 */
static void
restore_command_cache_path(const char *pgdata, char *path, size_t size)
{
	char buf[MAXPGPATH] = { 0 };

	join_path_components(buf, pgdata, RESTORE_COMMAND_CACHE_FILENAME);
	strlcpy(path, buf, size);
}


/*
 * restore_command_set_up writes the cache file used as this module's last
 * resort (step 3 in restore_command_resolve's own comment): one "key=value"
 * line per field, read back verbatim by restore_command_read_cache. Never
 * writes a password: PGPASSWORD/.pgpass is libpq's job, exactly as it
 * already is for a hand-written restore_command line.
 */
bool
restore_command_set_up(const char *pgdata, const RestoreCommandInfo *info)
{
	char path[MAXPGPATH] = { 0 };
	char contents[BUFSIZE] = { 0 };

	if (IS_EMPTY_STRING_BUFFER(info->host) ||
		IS_EMPTY_STRING_BUFFER(info->route) ||
		IS_EMPTY_STRING_BUFFER(info->user))
	{
		log_error("restore command --set-up requires --host, --route "
				  "and --user");
		return false;
	}

	restore_command_cache_path(pgdata, path, sizeof(path));

	int len = sformat(contents, sizeof(contents),
					  "host=%s\nport=%d\nroute=%s\nuser=%s\n",
					  info->host,
					  info->port > 0 ? info->port :
					  PG_AUTOCTL_ARCHIVER_SERVE_PORT,
					  info->route,
					  info->user);

	if (!write_file_atomic(contents, len, path))
	{
		log_error("Failed to write \"%s\"", path);
		return false;
	}

	log_info("Wrote restore command connection info to \"%s\"", path);

	return true;
}


/*
 * restore_command_read_cache reads back the cache file written by
 * restore_command_set_up (or by a previous successful monitor lookup, see
 * restore_command_resolve): a small parser, not a general-purpose INI
 * reader, because the file only ever holds these four lines.
 */
static bool
restore_command_read_cache(const char *pgdata, RestoreCommandInfo *info)
{
	char path[MAXPGPATH] = { 0 };
	char *contents = NULL;
	long size = 0L;
	bool found = false;

	restore_command_cache_path(pgdata, path, sizeof(path));

	if (!read_file_if_exists(path, &contents, &size) || size == 0)
	{
		return false;
	}

	char *ptr = contents;
	char *line = NULL;

	while ((line = strsep(&ptr, "\n")) != NULL) /* IGNORE-BANNED */
	{
		if (strncmp(line, "host=", 5) == 0)
		{
			strlcpy(info->host, line + 5, sizeof(info->host));
			found = true;
		}
		else if (strncmp(line, "port=", 5) == 0)
		{
			(void) stringToInt(line + 5, &(info->port));
		}
		else if (strncmp(line, "route=", 6) == 0)
		{
			strlcpy(info->route, line + 6, sizeof(info->route));
		}
		else if (strncmp(line, "user=", 5) == 0)
		{
			strlcpy(info->user, line + 5, sizeof(info->user));
		}
	}

	free(contents);

	return found;
}


/*
 * restore_command_resolve_from_node tries step 2: this node's own
 * pg_autoctl configuration file at pgdata, the same read
 * archiver_confirm_run() already does, then a single short-timeout
 * monitor_get_archiver_node() call -- the very same lookup `pg_autoctl
 * create postgres --from-archiver` already relies on
 * (keeper_get_archiver_node(), keeper.c) to learn an archiver's host and
 * serve port for a (formation, group), so this reuses an existing,
 * already-tested monitor API rather than inventing a new one.
 */
static bool
restore_command_resolve_from_node(const char *pgdata, RestoreCommandInfo *info)
{
	KeeperConfig config = { 0 };

	strlcpy(config.pgSetup.pgdata, pgdata, MAXPGPATH);

	if (!SetConfigFilePath(&(config.pathnames), pgdata) ||
		!keeper_config_read_file_skip_pgsetup(&config, true))
	{
		log_debug("restore command: no pg_autoctl configuration at \"%s\"",
				  pgdata);
		return false;
	}

	if (config.monitorDisabled)
	{
		log_debug("restore command: monitor disabled in \"%s\"", pgdata);
		return false;
	}

	Monitor monitor = { 0 };
	NodeAddress archiverNode = { 0 };
	bool found = false;

	pgconnect_timeout = RESTORE_COMMAND_TIMEOUT;

	if (!monitor_init(&monitor, config.monitor_pguri))
	{
		return false;
	}

	pgsql_set_retry_policy(&(monitor.pgsql.retryPolicy),
						   RESTORE_COMMAND_TIMEOUT, 0, 1000, 500);

	bool ok = monitor_get_archiver_node(&monitor, config.formation,
										config.groupId, &archiverNode, &found);

	pgsql_finish(&monitor.pgsql);

	if (!ok || !found)
	{
		log_debug("restore command: no archiver registered for "
				  "formation \"%s\" group %d", config.formation,
				  config.groupId);
		return false;
	}

	strlcpy(info->host, archiverNode.host, sizeof(info->host));
	info->port = archiverNode.port;
	sformat(info->route, sizeof(info->route), "%s/%d",
			config.formation, config.groupId);
	strlcpy(info->user, PG_AUTOCTL_REPLICA_USERNAME, sizeof(info->user));

	/* refresh the cache: next time the monitor is unreachable, use this */
	(void) restore_command_set_up(pgdata, info);

	return true;
}


/*
 * restore_command_resolve implements the three-step priority order
 * documented in restore_command.h.
 */
bool
restore_command_resolve(const char *pgdata, RestoreCommandInfo *info)
{
	RestoreCommandInfo cliInfo = *info;

	/* step 1: whatever was already given on the command line wins outright */
	if (!IS_EMPTY_STRING_BUFFER(cliInfo.host) &&
		!IS_EMPTY_STRING_BUFFER(cliInfo.route) &&
		!IS_EMPTY_STRING_BUFFER(cliInfo.user))
	{
		if (cliInfo.port <= 0)
		{
			cliInfo.port = PG_AUTOCTL_ARCHIVER_SERVE_PORT;
		}

		*info = cliInfo;
		return true;
	}

	/* step 2: this node's own pg_autoctl configuration and its monitor */
	RestoreCommandInfo fromNode = { 0 };

	if (restore_command_resolve_from_node(pgdata, &fromNode))
	{
		*info = fromNode;
		return true;
	}

	/* step 3: the cache file, from --set-up or a previous step-2 success */
	RestoreCommandInfo fromCache = { 0 };

	if (restore_command_read_cache(pgdata, &fromCache) &&
		!IS_EMPTY_STRING_BUFFER(fromCache.host) &&
		!IS_EMPTY_STRING_BUFFER(fromCache.route) &&
		!IS_EMPTY_STRING_BUFFER(fromCache.user))
	{
		if (fromCache.port <= 0)
		{
			fromCache.port = PG_AUTOCTL_ARCHIVER_SERVE_PORT;
		}

		*info = fromCache;
		return true;
	}

	return false;
}


/*
 * restore_command_run resolves the connection info then execv()s
 * `pg_walsender fetch-file`, found next to the running pg_autoctl binary
 * exactly as service_archiver_serve.c already does for `archiver serve`.
 */
int
restore_command_run(const char *pgdata, const RestoreCommandInfo *cliInfo,
					const char *sourceFile, const char *destFile)
{
	RestoreCommandInfo info = *cliInfo;

	if (!restore_command_resolve(pgdata, &info))
	{
		log_error("restore command: could not determine the archiver's "
				  "host/port/route/user -- this node is not a registered "
				  "pg_auto_failover node with a reachable monitor, and no "
				  "cache was found; run `pg_autoctl restore command "
				  "--set-up --host ... --route <formation>/<group> "
				  "--user ...` once, or pass --host/--route/--user "
				  "directly");
		return 1;
	}

	char pgWalsenderPath[MAXPGPATH] = { 0 };

	path_in_same_directory(pg_autoctl_program, "pg_walsender", pgWalsenderPath);

	if (!file_exists(pgWalsenderPath))
	{
		log_error("Failed to find pg_walsender at \"%s\"", pgWalsenderPath);
		return 1;
	}

	char portStr[16] = { 0 };

	sformat(portStr, sizeof(portStr), "%d", info.port);

	char *args[16];
	int argsIndex = 0;

	args[argsIndex++] = pgWalsenderPath;
	args[argsIndex++] = "fetch-file";
	args[argsIndex++] = "--host";
	args[argsIndex++] = info.host;
	args[argsIndex++] = "--port";
	args[argsIndex++] = portStr;
	args[argsIndex++] = "--route";
	args[argsIndex++] = info.route;
	args[argsIndex++] = "--user";
	args[argsIndex++] = info.user;
	args[argsIndex++] = "--filename";
	args[argsIndex++] = (char *) sourceFile;
	args[argsIndex++] = "--output";
	args[argsIndex++] = (char *) destFile;
	args[argsIndex] = NULL;

	log_debug("restore command: %s fetch-file --host %s --port %s "
			  "--route %s --user %s --filename %s --output %s",
			  pgWalsenderPath, info.host, portStr, info.route, info.user,
			  sourceFile, destFile);

	fflush(stdout);
	fflush(stderr);

	execv(pgWalsenderPath, args);

	/* execv only returns on failure */
	log_error("execv(\"%s\"): %m", pgWalsenderPath);
	return 1;
}
