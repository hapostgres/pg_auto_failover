/*
 * src/bin/pg_walserver/cli_fetch_systemid.c
 *   See cli_fetch_systemid.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <getopt.h>
#include <inttypes.h>
#include <string.h>

#include "postgres_fe.h"

#include "commandline.h"

#include "cli_common.h"
#include "cli_fetch_systemid.h"
#include "cli_root.h"
#include "defaults.h"
#include "env_utils.h"
#include "file_utils.h"
#include "log.h"
#include "pgctl.h"
#include "string_utils.h"

#define WS_SYSTEMID_FILENAME "pg_walserver_systemid"
#define WS_PGVERSION_FILENAME "pg_walserver_pgversion"

/* local helpers */
static bool read_existing_systemid(const char *path, uint64_t *out);
static bool read_existing_pgversion(const char *path, int *out);

static int cli_fetch_systemid_getopt(int argc, char **argv);
static void cli_fetch_systemid_command_run(int argc, char **argv);


/*
 * pg_walserver fetch-systemid --cluster <name> --pgdata <path> [--upstream ...]
 */

static char fetchSystemidPgdata[MAXPGPATH] = { 0 };
static char fetchSystemidConfigFile[MAXPGPATH] = { 0 };
static char fetchSystemidCluster[NAMEDATALEN + 16] = { 0 };
static char fetchSystemidPath[MAXPGPATH] = { 0 };
static char fetchSystemidUpstream[MAXCONNINFO] = { 0 };
static char fetchSystemidHost[_POSIX_HOST_NAME_MAX] = { 0 };
static char fetchSystemidPort[16] = { 0 };
static char fetchSystemidUser[NAMEDATALEN] = { 0 };
static bool fetchSystemidForce = false;

static struct option fetchSystemidLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'F' },
	{ "cluster", required_argument, NULL, 'c' },
	{ "path", required_argument, NULL, 'P' },
	{ "upstream", required_argument, NULL, 'u' },
	{ "host", required_argument, NULL, 'h' },
	{ "port", required_argument, NULL, 'p' },
	{ "user", required_argument, NULL, 'U' },
	{ "force", no_argument, NULL, 'f' },
	{ NULL, 0, NULL, 0 }
};

CommandLine fetch_systemid_command =
	make_command("fetch-systemid",
				 "Fetch a cluster's upstream system identifier",
				 "--cluster <name> --pgdata <path> [--config <path>] "
				 "| --path <dir> "
				 "[--upstream <conninfo> | --host <host> [--port <port>] "
				 "[--user <name>]] [--force]",
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --cluster   the cluster name to fetch for (looked up in "
				 "the config file)\n"
				 "  --path      the cluster's own directory (overrides the "
				 "cluster's own \"path\")\n"
				 "  --upstream  a libpq connection string to connect with "
				 "(overrides the\n"
				 "              cluster's own \"upstream\")\n"
				 "  --host / --port / --user  further override individual "
				 "connection\n"
				 "              parameters (default port: 5432, default "
				 "user: " PG_AUTOCTL_REPLICA_USERNAME ")\n"
													  "  --force     overwrite an already-recorded, different "
													  "system identifier\n",
				 cli_fetch_systemid_getopt, cli_fetch_systemid_command_run);


/*
 * read_existing_systemid reads "<path>/pg_walserver_systemid" if it exists,
 * returning the parsed identifier in *out. Returns false when the file is
 * absent or unparseable (treated as "no prior identifier", never a hard
 * error: a brand new cluster has no systemid file yet).
 */
static bool
read_existing_systemid(const char *path, uint64_t *out)
{
	char sysidPath[MAXPGPATH] = { 0 };
	char *contents = NULL;
	long size = 0L;

	sformat(sysidPath, sizeof(sysidPath), "%s/" WS_SYSTEMID_FILENAME, path);

	if (!read_file_if_exists(sysidPath, &contents, &size) ||
		contents == NULL || size == 0)
	{
		return false;
	}

	bool ok = stringToUInt64(contents, out);

	free(contents);

	return ok;
}


/*
 * read_existing_pgversion reads "<path>/pg_walserver_pgversion" if it
 * exists, returning the parsed server_version_num in *out. Returns false
 * when the file is absent or unparseable (treated as "no prior recorded
 * version", never a hard error: a brand new cluster, or one created before
 * this file existed, has no pgversion file yet).
 */
static bool
read_existing_pgversion(const char *path, int *out)
{
	char pgversionPath[MAXPGPATH] = { 0 };
	char *contents = NULL;
	long size = 0L;

	sformat(pgversionPath, sizeof(pgversionPath), "%s/" WS_PGVERSION_FILENAME,
			path);

	if (!read_file_if_exists(pgversionPath, &contents, &size) ||
		contents == NULL || size == 0)
	{
		return false;
	}

	bool ok = stringToInt(contents, out);

	free(contents);

	return ok;
}


bool
ws_fetch_systemid_execute(const WsUpstreamTarget *target, bool force,
						  uint64_t *systemIdentifierOut)
{
	ReplicationSource replicationSource = { 0 };

	replicationSource.primaryNode = target->node;
	strlcpy(replicationSource.userName, target->userName,
			sizeof(replicationSource.userName));
	strlcpy(replicationSource.applicationName, "pg_walserver_fetch_systemid",
			sizeof(replicationSource.applicationName));
	replicationSource.sslOptions = target->sslOptions;

	if (env_exists("PGPASSWORD"))
	{
		(void) get_env_copy("PGPASSWORD", replicationSource.password,
							sizeof(replicationSource.password));
	}

	log_info("Connecting to %s:%d as \"%s\" to fetch the system identifier",
			 target->node.host, target->node.port, target->userName);

	if (!pgctl_identify_system(&replicationSource))
	{
		/* errors have already been logged */
		return false;
	}

	uint64_t identifier = replicationSource.system.identifier;
	uint64_t existingIdentifier = 0;
	bool needSystemIdWrite = true;

	if (read_existing_systemid(target->path, &existingIdentifier))
	{
		if (existingIdentifier == identifier)
		{
			log_info("\"%s\" already has the correct system identifier "
					 "(%" PRIu64 ")", target->path, identifier);

			needSystemIdWrite = false;
		}
		else if (!force)
		{
			log_error("Refusing to overwrite the system identifier already "
					  "recorded for this cluster: %" PRIu64 " on disk, "
															"%" PRIu64
					  " from %s:%d -- this cluster's identity would "
					  "be changing under it, which usually means the wrong "
					  "upstream was given, or this cluster needs a fresh path "
					  "instead of reusing an old one",
					  existingIdentifier, identifier, target->node.host,
					  target->node.port);
			return false;
		}
		else
		{
			log_warn("Overwriting the system identifier recorded for \"%s\": "
					 "%" PRIu64 " -> %" PRIu64 " (--force)",
					 target->path, existingIdentifier, identifier);
		}
	}

	if (needSystemIdWrite)
	{
		char sysidPath[MAXPGPATH] = { 0 };
		char contents[64] = { 0 };

		sformat(sysidPath, sizeof(sysidPath), "%s/" WS_SYSTEMID_FILENAME,
				target->path);

		int len = sformat(contents, sizeof(contents), "%" PRIu64, identifier);

		if (!write_file_atomic(contents, len, sysidPath))
		{
			log_error("Failed to write \"%s\"", sysidPath);
			return false;
		}

		log_info("Wrote system identifier %" PRIu64 " to \"%s\"",
				 identifier, sysidPath);
	}

	/*
	 * Same overwrite-safety dance, its own file, for the upstream's major
	 * Postgres version -- see cli_basebackup.c's own use of this file to
	 * pick a version-safe pg_basebackup binary.
	 */
	int version = replicationSource.system.serverVersion;
	int existingVersion = 0;
	bool needVersionWrite = true;

	if (read_existing_pgversion(target->path, &existingVersion))
	{
		if (existingVersion == version)
		{
			log_info("\"%s\" already has the correct upstream Postgres "
					 "version (%d)", target->path, version);

			needVersionWrite = false;
		}
		else if (!force)
		{
			log_error("Refusing to overwrite the upstream Postgres version "
					  "already recorded for this cluster: %d on disk, %d "
					  "from %s:%d -- pass --force to overwrite it "
					  "deliberately",
					  existingVersion, version, target->node.host,
					  target->node.port);
			return false;
		}
		else
		{
			log_warn("Overwriting the upstream Postgres version recorded "
					 "for \"%s\": %d -> %d (--force)",
					 target->path, existingVersion, version);
		}
	}

	if (needVersionWrite)
	{
		char pgversionPath[MAXPGPATH] = { 0 };
		char contents[64] = { 0 };

		sformat(pgversionPath, sizeof(pgversionPath), "%s/" WS_PGVERSION_FILENAME,
				target->path);

		int len = sformat(contents, sizeof(contents), "%d", version);

		if (!write_file_atomic(contents, len, pgversionPath))
		{
			log_error("Failed to write \"%s\"", pgversionPath);
			return false;
		}

		log_info("Wrote upstream Postgres version %d to \"%s\"",
				 version, pgversionPath);
	}

	if (systemIdentifierOut != NULL)
	{
		*systemIdentifierOut = identifier;
	}

	return true;
}


/*
 * cli_fetch_systemid_getopt parses "pg_walserver fetch-systemid"'s own flags into the
 * file-scope statics above.
 */
static int
cli_fetch_systemid_getopt(int argc, char **argv)
{
	optind = 0;
	ws_prefill_pgdata_from_env(fetchSystemidPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:F:c:P:u:h:p:U:f",
							fetchSystemidLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(fetchSystemidPgdata, optarg, sizeof(fetchSystemidPgdata));
				break;
			}

			case 'F':
			{
				strlcpy(fetchSystemidConfigFile, optarg,
						sizeof(fetchSystemidConfigFile));
				break;
			}

			case 'c':
			{
				strlcpy(fetchSystemidCluster, optarg, sizeof(fetchSystemidCluster));
				break;
			}

			case 'P':
			{
				strlcpy(fetchSystemidPath, optarg, sizeof(fetchSystemidPath));
				break;
			}

			case 'u':
			{
				strlcpy(fetchSystemidUpstream, optarg, sizeof(fetchSystemidUpstream));
				break;
			}

			case 'h':
			{
				strlcpy(fetchSystemidHost, optarg, sizeof(fetchSystemidHost));
				break;
			}

			case 'p':
			{
				strlcpy(fetchSystemidPort, optarg, sizeof(fetchSystemidPort));
				break;
			}

			case 'U':
			{
				strlcpy(fetchSystemidUser, optarg, sizeof(fetchSystemidUser));
				break;
			}

			case 'f':
			{
				fetchSystemidForce = true;
				break;
			}

			default:
			{
				commandline_print_usage(&ws_root, stderr);
				exit(1);
			}
		}
	}

	return optind;
}


/*
 * cli_fetch_systemid_command_run runs "pg_walserver fetch-systemid" against
 * the options cli_fetch_systemid_getopt parsed above, then exit()s with its
 * own result.
 */
static void
cli_fetch_systemid_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	WsUpstreamTarget target = { 0 };

	if (!cli_resolve_upstream(fetchSystemidPgdata, fetchSystemidConfigFile,
							  fetchSystemidCluster,
							  fetchSystemidPath, fetchSystemidUpstream,
							  fetchSystemidHost, fetchSystemidPort,
							  fetchSystemidUser, &target))
	{
		exit(1);
	}

	exit(ws_fetch_systemid_execute(&target, fetchSystemidForce, NULL) ? 0 : 1);
}
