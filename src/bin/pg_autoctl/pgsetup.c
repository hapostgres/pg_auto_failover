/*
 * src/bin/pg_autoctl/pgsetup.c
 *   Discovers a PostgreSQL setup by calling pg_controldata and reading
 *   postmaster.pid file, getting clues from the process environment and from
 *   user given hints (options).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "parson.h"

#include "postgres_fe.h"
#include "pqexpbuffer.h"

#include "defaults.h"
#include "env_utils.h"
#include "log.h"
#include "parsing.h"
#include "pgctl.h"
#include "signals.h"
#include "string_utils.h"


static bool get_pgpid(PostgresSetup *pgSetup, bool pgIsNotRunningIsOk);
static PostmasterStatus pmStatusFromString(const char *postmasterStatus);


/*
 * Discover PostgreSQL environment from given clues, or a partial setup.
 *
 * This routines check the PATH for pg_ctl, and is ok when there's a single
 * entry in found. It then uses either given PGDATA or the environment value
 * and runs a pg_controldata to get system identifier and PostgreSQL version
 * numbers. Then it reads PGDATA/postmaster.pid to get the pid and the port of
 * the running PostgreSQL server. Then it can connects to it and see if it's in
 * recovery.
 */
bool
pg_setup_init(PostgresSetup *pgSetup,
			  PostgresSetup *options,
			  bool missing_pgdata_is_ok,
			  bool pg_is_not_running_is_ok)
{
	int errors = 0;

	/*
	 * Make sure that we keep the options->nodeKind in the pgSetup.
	 */
	pgSetup->pgKind = options->pgKind;

	/*
	 * Also make sure that we keep the pg_controldata results if we have them.
	 */
	pgSetup->control = options->control;

	/*
	 * Also make sure that we keep the hbaLevel to edit. Remember that
	 * --skip-pg-hba is registered in the config as --auth skip.
	 */
	if (strcmp(options->authMethod, "skip") == 0)
	{
		pgSetup->hbaLevel = HBA_EDIT_SKIP;
		strlcpy(pgSetup->hbaLevelStr, options->authMethod, NAMEDATALEN);
	}
	else
	{
		pgSetup->hbaLevel = options->hbaLevel;
		strlcpy(pgSetup->hbaLevelStr, options->hbaLevelStr, NAMEDATALEN);
	}

	/*
	 * Make sure that we keep the SSL options too.
	 */
	pgSetup->ssl.active = options->ssl.active;
	pgSetup->ssl.createSelfSignedCert = options->ssl.createSelfSignedCert;
	pgSetup->ssl.sslMode = options->ssl.sslMode;
	strlcpy(pgSetup->ssl.sslModeStr, options->ssl.sslModeStr, SSL_MODE_STRLEN);
	strlcpy(pgSetup->ssl.caFile, options->ssl.caFile, MAXPGPATH);
	strlcpy(pgSetup->ssl.crlFile, options->ssl.crlFile, MAXPGPATH);
	strlcpy(pgSetup->ssl.serverCert, options->ssl.serverCert, MAXPGPATH);
	strlcpy(pgSetup->ssl.serverKey, options->ssl.serverKey, MAXPGPATH);

	/* Also make sure we keep the citus specific clusterName option */
	strlcpy(pgSetup->citusClusterName, options->citusClusterName, NAMEDATALEN);

	/* check or find pg_ctl, unless we already have it */
	if (IS_EMPTY_STRING_BUFFER(pgSetup->pg_ctl) ||
		IS_EMPTY_STRING_BUFFER(pgSetup->pg_version))
	{
		if (!IS_EMPTY_STRING_BUFFER(options->pg_ctl))
		{
			/* copy over pg_ctl and pg_version */
			strlcpy(pgSetup->pg_ctl, options->pg_ctl, MAXPGPATH);
			strlcpy(pgSetup->pg_version, options->pg_version,
					PG_VERSION_STRING_MAX);

			/* we might not have fetched the version yet */
			if (IS_EMPTY_STRING_BUFFER(pgSetup->pg_version))
			{
				/* also cache the version in options */
				if (!pg_ctl_version(options))
				{
					/* we already logged about it */
					return false;
				}

				strlcpy(pgSetup->pg_version,
						options->pg_version,
						sizeof(pgSetup->pg_version));

				log_debug("pg_setup_init: %s version %s",
						  pgSetup->pg_ctl, pgSetup->pg_version);
			}
		}
		else
		{
			if (!config_find_pg_ctl(pgSetup))
			{
				/* config_find_pg_ctl already logged errors */
				errors++;
			}
		}
	}

	/* check or find PGDATA */
	if (options->pgdata[0] != '\0')
	{
		strlcpy(pgSetup->pgdata, options->pgdata, MAXPGPATH);
	}
	else
	{
		if (!get_env_pgdata(pgSetup->pgdata))
		{
			log_error("Failed to set PGDATA either from the environment "
					  "or from --pgdata");
			errors++;
		}
	}

	if (!missing_pgdata_is_ok && !directory_exists(pgSetup->pgdata))
	{
		log_fatal("Database directory \"%s\" not found", pgSetup->pgdata);
		return false;
	}
	else if (!missing_pgdata_is_ok)
	{
		char globalControlPath[MAXPGPATH] = { 0 };

		/* globalControlFilePath = $PGDATA/global/pg_control */
		join_path_components(globalControlPath,
							 pgSetup->pgdata, "global/pg_control");

		if (!file_exists(globalControlPath))
		{
			log_error("PGDATA exists but is not a Postgres directory, "
					  "see above for details");
			return false;
		}
	}

	/* get the real path of PGDATA now */
	if (directory_exists(pgSetup->pgdata))
	{
		if (!normalize_filename(pgSetup->pgdata, pgSetup->pgdata, MAXPGPATH))
		{
			/* errors have already been logged */
			return false;
		}
	}

	/* check of find username */
	if (options->username[0] != '\0')
	{
		strlcpy(pgSetup->username, options->username, NAMEDATALEN);
	}
	else
	{
		/*
		 * If a PGUSER environment variable is defined, take the value from
		 * there. Otherwise we attempt to connect without username. In that
		 * case the username will be determined based on the current user.
		 */
		if (!get_env_copy_with_fallback("PGUSER", pgSetup->username, NAMEDATALEN, ""))
		{
			/* errors have already been logged */
			return false;
		}
	}

	/* check or find dbname */
	if (options->dbname[0] != '\0')
	{
		strlcpy(pgSetup->dbname, options->dbname, NAMEDATALEN);
	}
	else
	{
		/*
		 * If a PGDATABASE environment variable is defined, take the value from
		 * there. Otherwise we attempt to connect without a database name, and
		 * the default will use the username here instead.
		 */
		if (!get_env_copy_with_fallback("PGDATABASE", pgSetup->dbname, NAMEDATALEN,
										DEFAULT_DATABASE_NAME))
		{
			/* errors have already been logged */
			return false;
		}
	}

	/*
	 * Read the postmaster.pid file to find out pid, port and unix socket
	 * directory of a running PostgreSQL instance.
	 */
	bool pgIsReady = pg_setup_is_ready(pgSetup, pg_is_not_running_is_ok);

	if (!pgIsReady && !pg_is_not_running_is_ok)
	{
		/* errors have already been logged */
		errors++;
	}

	/*
	 * check or find PGHOST
	 *
	 * By order of preference, we use:
	 *  --pghost     command line option
	 *  PGDATA/postmaster.pid
	 *  PGHOST       from the environment
	 */
	if (options->pghost[0] != '\0')
	{
		strlcpy(pgSetup->pghost, options->pghost, _POSIX_HOST_NAME_MAX);
	}
	else
	{
		/* read_pg_pidfile might already have set pghost for us */
		if (pgSetup->pghost[0] == '\0')
		{
			/*
			 * We can (at least try to) connect without host= in the connection
			 * string, so missing PGHOST and --pghost isn't an error.
			 */
			if (!get_env_copy_with_fallback("PGHOST", pgSetup->pghost,
											_POSIX_HOST_NAME_MAX, ""))
			{
				/* errors have already been logged */
				return false;
			}
		}
	}

	/*
	 * In test environment we might disable unix socket directories. In that
	 * case, we need to have an host to connect to, accepting to connect
	 * without host= in the connection string is not going to cut it.
	 */
	if (IS_EMPTY_STRING_BUFFER(pgSetup->pghost))
	{
		if (env_found_empty("PG_REGRESS_SOCK_DIR"))
		{
			log_error("PG_REGRESS_SOCK_DIR is set to \"\" to disable unix "
					  "socket directories, now --pghost is mandatory, "
					  "but unset.");
			errors++;
		}
	}

	/* check or find PGPORT
	 *
	 * By order or preference, we use:
	 *   --pgport       command line option
	 *   PGDATA/postmaster.pid
	 *   PGPORT         from the environment
	 *   POSTGRES_PORT  from our hard coded defaults (5432, see defaults.h)
	 */
	if (options->pgport > 0)
	{
		pgSetup->pgport = options->pgport;
	}
	else
	{
		/* if we have a running cluster, just use its port */
		if (pgSetup->pidFile.pid > 0 && pgSetup->pidFile.port > 0)
		{
			pgSetup->pgport = pgSetup->pidFile.port;
		}
		else
		{
			/*
			 * no running cluster, what about using PGPORT then?
			 */
			pgSetup->pgport = pgsetup_get_pgport();
		}
	}

	/* Set proxy port */
	if (options->proxyport > 0)
	{
		pgSetup->proxyport = options->proxyport;
	}


	/*
	 * If --listen is given, then set our listen_addresses to this value
	 */
	if (!IS_EMPTY_STRING_BUFFER(options->listen_addresses))
	{
		strlcpy(pgSetup->listen_addresses,
				options->listen_addresses, MAXPGPATH);
	}
	else
	{
		/*
		 * The default listen_addresses is '*', because we are dealing with a
		 * cluster setup and 'localhost' isn't going to cut it: the monitor and
		 * the coordinator nodes need to be able to connect to our local node
		 * using a connection string with hostname:port.
		 */
		strlcpy(pgSetup->listen_addresses,
				POSTGRES_DEFAULT_LISTEN_ADDRESSES, MAXPGPATH);
	}


	/*
	 * If --auth is given, then set our authMethod to this value
	 * otherwise it remains empty
	 */
	if (!IS_EMPTY_STRING_BUFFER(options->authMethod))
	{
		strlcpy(pgSetup->authMethod,
				options->authMethod, NAMEDATALEN);
	}

	pgSetup->settings = options->settings;

	/*
	 * And we always double-check with PGDATA/postmaster.pid if we have it, and
	 * we should have it in the normal/expected case.
	 */
	if (pgIsReady &&
		pgSetup->pidFile.pid > 0 &&
		pgSetup->pgport != pgSetup->pidFile.port)
	{
		log_error("Given --pgport %d doesn't match PostgreSQL "
				  "port %d from \"%s/postmaster.pid\"",
				  pgSetup->pgport, pgSetup->pidFile.port, pgSetup->pgdata);
		errors++;
	}

	/*
	 * When we have a PGDATA and Postgres is not running, we need to grab more
	 * information about the local installation: pg_controldata can give us the
	 * pg-_control_version, catalog_version_no, and system_identifier.
	 */
	if (errors == 0)
	{
		/*
		 * Only run pg_controldata when Postgres is not running, otherwise we
		 * get the same information later from an SQL query, see
		 * pgsql_get_postgres_metadata.
		 */
		if (!pg_setup_is_running(pgSetup) &&
			pgSetup->control.pg_control_version == 0)
		{
			pg_controldata(pgSetup, missing_pgdata_is_ok);

			if (pgSetup->control.pg_control_version == 0)
			{
				/* we already logged about it */
				if (!missing_pgdata_is_ok)
				{
					errors++;
				}
			}
			else
			{
				log_debug("Found PostgreSQL system %" PRIu64 " at \"%s\", "
															 "version %u, catalog version %u",
						  pgSetup->control.system_identifier,
						  pgSetup->pgdata,
						  pgSetup->control.pg_control_version,
						  pgSetup->control.catalog_version_no);
			}
		}
	}

	/*
	 * Sometimes `pg_ctl start` returns with success and Postgres is still in
	 * crash recovery replaying WAL files, in the "starting" state rather than
	 * the "ready" state.
	 *
	 * In that case, we wait until Postgres is ready for connections. The whole
	 * pg_autoctl code is expecting to be able to connect to Postgres, so
	 * there's no point in returning now and having the next connection attempt
	 * fail with something like the following:
	 *
	 * ERROR Connection to database failed: FATAL: the database system is
	 * starting up
	 */
	if (pgSetup->pidFile.port > 0 &&
		pgSetup->pgport == pgSetup->pidFile.port)
	{
		if (!pgIsReady)
		{
			if (!pg_is_not_running_is_ok)
			{
				log_error("Failed to read Postgres pidfile, "
						  "see above for details");
				return false;
			}
		}
	}

	if (errors > 0)
	{
		log_fatal("Failed to discover PostgreSQL setup, "
				  "please fix previous errors.");
		return false;
	}

	return true;
}


/*
 * Read the first line of the PGDATA/postmaster.pid file to get Postgres PID.
 */
static bool
get_pgpid(PostgresSetup *pgSetup, bool pgIsNotRunningIsOk)
{
	char *contents = NULL;
	long fileSize = 0;
	char pidfile[MAXPGPATH];
	char *lines[1];
	int pid = -1;

	/* when !pgIsNotRunningIsOk then log_error(), otherwise log_debug() */
	int logLevel = pgIsNotRunningIsOk ? LOG_TRACE : LOG_ERROR;

	join_path_components(pidfile, pgSetup->pgdata, "postmaster.pid");

	if (!read_file_if_exists(pidfile, &contents, &fileSize))
	{
		log_level(logLevel, "Failed to open file \"%s\": %m", pidfile);

		if (!pgIsNotRunningIsOk)
		{
			log_info("Is PostgreSQL at \"%s\" up and running?", pgSetup->pgdata);
		}
		return false;
	}

	if (fileSize == 0)
	{
		/* yeah, that happens (race condition, kind of) */
		log_debug("The PID file \"%s\" is empty", pidfile);
		free(contents);
		return false;
	}
	else if (splitLines(contents, lines, 1) != 1 ||
			 !stringToInt(lines[0], &pid))
	{
		log_warn("Invalid data in PID file \"%s\"", pidfile);
		free(contents);
		return false;
	}

	free(contents);
	contents = NULL;

	/* postmaster PID (or negative of a standalone backend's PID) */
	if (pid < 0)
	{
		int standalonePid = -1 * pid;

		if (kill(standalonePid, 0) == 0)
		{
			pgSetup->pidFile.pid = pid;
			return true;
		}
		log_debug("Read a stale standalone pid in \"postmaster.pid\": %d", pid);
		return false;
	}
	else if (pid > 0 && pid <= INT_MAX)
	{
		if (kill(pid, 0) == 0)
		{
			pgSetup->pidFile.pid = pid;
			return true;
		}
		else
		{
			int logLevel = pgIsNotRunningIsOk ? LOG_DEBUG : LOG_WARN;

			log_level(logLevel,
					  "Read a stale pid in \"postmaster.pid\": %d", pid);

			return false;
		}
	}
	else
	{
		/* that's more like a bug, really */
		log_error("Invalid PID \"%d\" read in \"postmaster.pid\"", pid);
		return false;
	}
}


/*
 * Read the PGDATA/postmaster.pid file to get the port number of the running
 * server we're asked to keep highly available.
 */
bool
read_pg_pidfile(PostgresSetup *pgSetup, bool pgIsNotRunningIsOk, int maxRetries)
{
	FILE *fp;
	int lineno;
	char line[BUFSIZE];
	char pidfile[MAXPGPATH];

	join_path_components(pidfile, pgSetup->pgdata, "postmaster.pid");

	if ((fp = fopen_read_only(pidfile)) == NULL)
	{
		/*
		 * Maybe we're attempting to read the file during Postgres start-up
		 * phase and we just got where the file is replaced, when going from
		 * standalone backend to full service.
		 */
		if (maxRetries > 0)
		{
			log_trace("read_pg_pidfile: \"%s\" does not exist [%d]",
					  pidfile, maxRetries);
			pg_usleep(250 * 1000); /* wait for 250ms and try again */
			return read_pg_pidfile(pgSetup, pgIsNotRunningIsOk, maxRetries - 1);
		}

		if (!pgIsNotRunningIsOk)
		{
			log_error("Failed to open file \"%s\": %m", pidfile);
			log_info("Is PostgreSQL at \"%s\" up and running?", pgSetup->pgdata);
		}
		return false;
	}

	for (lineno = 1; lineno <= LOCK_FILE_LINE_PM_STATUS; lineno++)
	{
		if (fgets(line, sizeof(line), fp) == NULL)
		{
			/* later lines are added during start-up, will appear later */
			if (lineno > LOCK_FILE_LINE_PORT)
			{
				/* that's retry-able */
				fclose(fp);

				if (maxRetries == 0)
				{
					/* partial read is ok, pgSetup keeps track */
					return true;
				}

				pg_usleep(250 * 1000); /* sleep for 250ms */
				log_trace("read_pg_pidfile: fgets is NULL for lineno %d, retry %d",
						  lineno, maxRetries);
				return read_pg_pidfile(pgSetup,
									   pgIsNotRunningIsOk,
									   maxRetries - 1);
			}
			else
			{
				/* don't use %m to print errno, errno is not set by fgets */
				log_error("Failed to read line %d from file \"%s\"",
						  lineno, pidfile);
				fclose(fp);
				return false;
			}
		}

		int lineLength = strlen(line);

		/* chomp the ending Newline (\n) */
		if (lineLength > 0)
		{
			line[lineLength - 1] = '\0';
			lineLength = strlen(line);
		}

		if (lineno == LOCK_FILE_LINE_PID)
		{
			int pid = 0;
			if (!stringToInt(line, &pid))
			{
				log_error("Postgres pidfile does not contain a valid pid %s",
						  line);

				return false;
			}

			/* a standalone backend pid is negative, we signal the actual pid */
			pgSetup->pidFile.pid = abs(pid);

			if (kill(pgSetup->pidFile.pid, 0) != 0)
			{
				log_error("Postgres pidfile contains pid %d, "
						  "which is not running", pgSetup->pidFile.pid);

				/* well then reset the PID to our unknown value */
				pgSetup->pidFile.pid = 0;

				return false;
			}

			if (pid < 0)
			{
				/* standalone backend during the start-up process */
				break;
			}
		}

		if (lineno == LOCK_FILE_LINE_PORT)
		{
			if (!stringToUShort(line, &pgSetup->pidFile.port))
			{
				log_error("Postgres pidfile does not contain a valid port %s",
						  line);

				return false;
			}
		}

		if (lineno == LOCK_FILE_LINE_SOCKET_DIR)
		{
			if (lineLength > 0)
			{
				int n = strlcpy(pgSetup->pghost, line, _POSIX_HOST_NAME_MAX);

				if (n >= _POSIX_HOST_NAME_MAX)
				{
					log_error("Failed to read unix socket directory \"%s\" "
							  "from file \"%s\": the directory name is %d "
							  "characters long, "
							  "and pg_autoctl only accepts up to %d characters",
							  line, pidfile, n, _POSIX_HOST_NAME_MAX - 1);
					return false;
				}
			}
		}

		if (lineno == LOCK_FILE_LINE_PM_STATUS)
		{
			if (lineLength > 0)
			{
				pgSetup->pm_status = pmStatusFromString(line);
			}
		}
	}
	fclose(fp);

	log_trace("read_pg_pidfile: pid %d, port %d, host %s, status \"%s\"",
			  pgSetup->pidFile.pid,
			  pgSetup->pidFile.port,
			  pgSetup->pghost,
			  pmStatusToString(pgSetup->pm_status));

	return true;
}


/*
 * fprintf_pg_setup prints to given STREAM the current setting found in
 * pgSetup.
 */
void
fprintf_pg_setup(FILE *stream, PostgresSetup *pgSetup)
{
	int pgversion = 0;

	(void) parse_pg_version_string(pgSetup->pg_version, &pgversion);

	fformat(stream, "pgdata:                %s\n", pgSetup->pgdata);
	fformat(stream, "pg_ctl:                %s\n", pgSetup->pg_ctl);

	fformat(stream, "pg_version:            \"%s\" (%d)\n",
			pgSetup->pg_version, pgversion);

	fformat(stream, "pghost:                %s\n", pgSetup->pghost);
	fformat(stream, "pgport:                %d\n", pgSetup->pgport);
	fformat(stream, "proxyport:             %d\n", pgSetup->proxyport);
	fformat(stream, "pid:                   %d\n", pgSetup->pidFile.pid);
	fformat(stream, "is in recovery:        %s\n",
			pgSetup->is_in_recovery ? "yes" : "no");
	fformat(stream, "Control cluster state: %s\n",
			dbstateToString(pgSetup->control.state));
	fformat(stream, "Control Version:       %u\n",
			pgSetup->control.pg_control_version);
	fformat(stream, "Catalog Version:       %u\n",
			pgSetup->control.catalog_version_no);
	fformat(stream, "System Identifier:     %" PRIu64 "\n",
			pgSetup->control.system_identifier);
	fformat(stream, "Latest checkpoint LSN: %s\n",
			pgSetup->control.latestCheckpointLSN);
	fformat(stream, "Postmaster status:     %s\n",
			pmStatusToString(pgSetup->pm_status));
	fflush(stream);
}


/*
 * pg_setup_as_json copies in the given pre-allocated string the json
 * representation of the pgSetup.
 */
bool
pg_setup_as_json(PostgresSetup *pgSetup, JSON_Value *js)
{
	JSON_Object *jsobj = json_value_get_object(js);
	char system_identifier[BUFSIZE];

	json_object_set_string(jsobj, "pgdata", pgSetup->pgdata);
	json_object_set_string(jsobj, "pg_ctl", pgSetup->pg_ctl);
	json_object_set_string(jsobj, "version", pgSetup->pg_version);
	json_object_set_string(jsobj, "host", pgSetup->pghost);
	json_object_set_number(jsobj, "port", (double) pgSetup->pgport);
	json_object_set_number(jsobj, "proxyport", (double) pgSetup->proxyport);
	json_object_set_number(jsobj, "pid", (double) pgSetup->pidFile.pid);
	json_object_set_boolean(jsobj, "in_recovery", pgSetup->is_in_recovery);

	json_object_dotset_number(jsobj,
							  "control.version",
							  (double) pgSetup->control.pg_control_version);

	json_object_dotset_number(jsobj,
							  "control.catalog_version",
							  (double) pgSetup->control.catalog_version_no);

	sformat(system_identifier, BUFSIZE, "%" PRIu64,
			pgSetup->control.system_identifier);
	json_object_dotset_string(jsobj,
							  "control.system_identifier",
							  system_identifier);

	json_object_dotset_string(jsobj,
							  "postmaster.status",
							  pmStatusToString(pgSetup->pm_status));
	return true;
}


/*
 * pg_setup_get_local_connection_string build a connecting string to connect
 * to the local postgres server and writes it to connectionString, which should
 * be at least MAXCONNINFO in size.
 */
bool
pg_setup_get_local_connection_string(PostgresSetup *pgSetup,
									 char *connectionString)
{
	char pg_regress_sock_dir[MAXPGPATH] = { 0 };
	bool pg_regress_sock_dir_exists = env_exists("PG_REGRESS_SOCK_DIR");
	PQExpBuffer connStringBuffer = createPQExpBuffer();

	if (connStringBuffer == NULL)
	{
		log_error("Failed to allocate memory");
		return false;
	}

	appendPQExpBuffer(connStringBuffer, "port=%d dbname=%s",
					  pgSetup->pgport, pgSetup->dbname);

	if (pg_regress_sock_dir_exists &&
		!get_env_copy("PG_REGRESS_SOCK_DIR", pg_regress_sock_dir, MAXPGPATH))
	{
		/* errors have already been logged */
		destroyPQExpBuffer(connStringBuffer);
		return false;
	}

	/*
	 * When PG_REGRESS_SOCK_DIR is set and empty, we force the connection
	 * string to use "localhost" (TCP/IP hostname for IP 127.0.0.1 or ::1,
	 * usually), even when the configuration setup is using a unix directory
	 * setting.
	 */
	if (env_found_empty("PG_REGRESS_SOCK_DIR") &&
		(IS_EMPTY_STRING_BUFFER(pgSetup->pghost) ||
		 pgSetup->pghost[0] == '/'))
	{
		appendPQExpBufferStr(connStringBuffer, " host=localhost");
	}
	else if (!IS_EMPTY_STRING_BUFFER(pgSetup->pghost))
	{
		if (pg_regress_sock_dir_exists && strlen(pg_regress_sock_dir) > 0 &&
			strcmp(pgSetup->pghost, pg_regress_sock_dir) != 0)
		{
			/*
			 * It might turn out ok (stray environment), but in case of
			 * connection error, this warning should be useful to debug the
			 * situation.
			 */
			log_warn("PG_REGRESS_SOCK_DIR is set to \"%s\", "
					 "and our setup is using \"%s\"",
					 pg_regress_sock_dir,
					 pgSetup->pghost);
		}
		appendPQExpBuffer(connStringBuffer, " host=%s", pgSetup->pghost);
	}

	if (!IS_EMPTY_STRING_BUFFER(pgSetup->username))
	{
		appendPQExpBuffer(connStringBuffer, " user=%s", pgSetup->username);
	}

	if (PQExpBufferBroken(connStringBuffer))
	{
		log_error("Failed to allocate memory");
		destroyPQExpBuffer(connStringBuffer);
		return false;
	}

	if (strlcpy(connectionString,
				connStringBuffer->data, MAXCONNINFO) >= MAXCONNINFO)
	{
		log_error("Failed to copy connection string \"%s\" which is %lu bytes "
				  "long, pg_autoctl only supports connection strings up to "
				  " %lu bytes",
				  connStringBuffer->data,
				  (unsigned long) connStringBuffer->len,
				  (unsigned long) MAXCONNINFO);
		destroyPQExpBuffer(connStringBuffer);
		return false;
	}

	destroyPQExpBuffer(connStringBuffer);
	return true;
}


/*
 * pg_setup_pgdata_exists returns true when PGDATA exists, hosts a
 * global/pg_control file (so that it looks like a Postgres cluster) and when
 * the pg_controldata probe was successful.
 */
bool
pg_setup_pgdata_exists(PostgresSetup *pgSetup)
{
	char globalControlPath[MAXPGPATH] = { 0 };

	/* make sure our cached value in pgSetup still makes sense */
	if (!directory_exists(pgSetup->pgdata))
	{
		return false;
	}

	/* globalControlFilePath = $PGDATA/global/pg_control */
	join_path_components(globalControlPath, pgSetup->pgdata, "global/pg_control");

	if (!file_exists(globalControlPath))
	{
		return false;
	}

	/*
	 * Now that we know that PGDATA exists, let's grab the system identifier if
	 * we don't have it already.
	 */
	if (pgSetup->control.system_identifier == 0)
	{
		bool missingPgdataIsOk = false;

		/* errors are logged from within pg_controldata */
		(void) pg_controldata(pgSetup, missingPgdataIsOk);

		return pgSetup->control.system_identifier != 0;
	}

	return true;
}


/*
 * pg_setup_pgdata_exists returns true when the pg_controldata probe was
 * susccessful.
 */
bool
pg_setup_is_running(PostgresSetup *pgSetup)
{
	bool pgIsNotRunningIsOk = true;

	return pgSetup->pidFile.pid != 0

	       /* if we don't have the PID yet, try reading it now */
		   || (get_pgpid(pgSetup, pgIsNotRunningIsOk) &&
			   pgSetup->pidFile.pid > 0);
}


/*
 * pg_setup_is_ready returns true when the postmaster.pid file has a "ready"
 * status in it, which we parse in pgSetup->pm_status.
 */
bool
pg_setup_is_ready(PostgresSetup *pgSetup, bool pgIsNotRunningIsOk)
{
	char globalControlPath[MAXPGPATH] = { 0 };

	/* globalControlFilePath = $PGDATA/global/pg_control */
	join_path_components(globalControlPath, pgSetup->pgdata, "global/pg_control");

	if (!file_exists(globalControlPath))
	{
		return false;
	}

	/*
	 * Invalidate in-memory Postmaster status cache.
	 *
	 * This makes sure we enter the main loop and attempt to read the
	 * postmaster.pid file at least once: if Postgres was stopped, then the
	 * file that we've read previously might not exists anymore.
	 */
	pgSetup->pm_status = POSTMASTER_STATUS_UNKNOWN;

	/*
	 * Sometimes `pg_ctl start` returns with success and Postgres is still
	 * in crash recovery replaying WAL files, in the "starting" state
	 * rather than the "ready" state.
	 *
	 * In that case, we wait until Postgres is ready for connections. The
	 * whole pg_autoctl code is expecting to be able to connect to
	 * Postgres, so there's no point in returning now and having the next
	 * connection attempt fail with something like the following:
	 *
	 * ERROR Connection to database failed: FATAL: the database system is
	 * starting up
	 */
	while (pgSetup->pm_status != POSTMASTER_STATUS_READY)
	{
		int maxRetries = 5;

		if (!get_pgpid(pgSetup, pgIsNotRunningIsOk))
		{
			/*
			 * We failed to read the Postgres pid file, and infinite
			 * looping might not help here anymore. Better give control
			 * back to the launching process (might be init scripts,
			 * systemd or the like) so that they may log a transient
			 * failure and try again.
			 */
			if (!pgIsNotRunningIsOk)
			{
				log_error("Failed to get Postgres pid, "
						  "see above for details");
			}

			/*
			 * we failed to get Postgres pid from the first line of its pid
			 * file, so we consider that Postgres is not running, thus not
			 * ready.
			 */
			return false;
		}

		/*
		 * When starting up we might read the postmaster.pid file too
		 * early, when Postgres is still in its "standalone backend" phase.
		 * Let's give it 250ms before trying again then.
		 */
		if (pgSetup->pidFile.pid < 0)
		{
			pg_usleep(250 * 1000);
			continue;
		}

		/*
		 * Here, we know that Postgres is running, and we even have its
		 * PID. Time to try and read the rest of the PID file. This might
		 * fail when the file isn't complete yet, in which case we're going
		 * to retry.
		 */
		if (!read_pg_pidfile(pgSetup, pgIsNotRunningIsOk, maxRetries))
		{
			log_warn("Failed to read Postgres \"postmaster.pid\" file");
			return false;
		}

		/* avoid an extra wait if that's possible */
		if (pgSetup->pm_status == POSTMASTER_STATUS_READY)
		{
			break;
		}

		log_debug("postmaster status is \"%s\", retrying in %ds.",
				  pmStatusToString(pgSetup->pm_status),
				  PG_AUTOCTL_KEEPER_RETRY_TIME_MS);

		pg_usleep(PG_AUTOCTL_KEEPER_RETRY_TIME_MS * 1000);
	}

	if (pgSetup->pm_status != POSTMASTER_STATUS_UNKNOWN)
	{
		log_trace("pg_setup_is_ready: %s", pmStatusToString(pgSetup->pm_status));
	}

	return pgSetup->pm_status == POSTMASTER_STATUS_READY;
}


/*
 * pg_setup_wait_until_is_ready loops over pg_setup_is_running() and returns
 * when Postgres is ready. The loop tries every 100ms up to the given timeout,
 * given in seconds.
 */
bool
pg_setup_wait_until_is_ready(PostgresSetup *pgSetup, int timeout, int logLevel)
{
	uint64_t startTime = time(NULL);
	int attempts = 0;

	pid_t previousPostgresPid = pgSetup->pidFile.pid;
	bool pgIsRunning = false;
	bool pgIsReady = false;

	bool missingPgdataIsOk = false;
	bool postgresNotRunningIsOk = true;

	log_trace("pg_setup_wait_until_is_ready");

	for (attempts = 1; !pgIsRunning; attempts++)
	{
		uint64_t now = time(NULL);

		/* sleep 100 ms in between postmaster.pid probes */
		pg_usleep(100 * 1000);

		pgIsRunning = get_pgpid(pgSetup, postgresNotRunningIsOk) &&
					  pgSetup->pidFile.pid > 0;

		/* let's not be THAT verbose about it */
		if ((attempts - 1) % 10 == 0)
		{
			log_debug("pg_setup_wait_until_is_ready(): postgres %s, "
					  "pid %d (was %d), after %ds and %d attempt(s)",
					  pgIsRunning ? "is running" : "is not running",
					  pgSetup->pidFile.pid,
					  previousPostgresPid,
					  (int) (now - startTime),
					  attempts);
		}

		/* we're done if we reach the timeout */
		if ((now - startTime) >= timeout)
		{
			break;
		}
	}

	/*
	 * Now update our pgSetup from the running database, including versions and
	 * all we can discover.
	 */
	if (pgIsRunning && previousPostgresPid != pgSetup->pidFile.pid)
	{
		/*
		 * Update our pgSetup view of Postgres once we have made sure it's
		 * running.
		 */
		PostgresSetup newPgSetup = { 0 };

		if (!pg_setup_init(&newPgSetup,
						   pgSetup,
						   missingPgdataIsOk,
						   postgresNotRunningIsOk))
		{
			/* errors have already been logged */
			log_error("pg_setup_wait_until_is_ready: pg_setup_init is false");
			return false;
		}

		*pgSetup = newPgSetup;

		/* avoid an extra pg_setup_is_ready call if we're all good already */
		pgIsReady = pgSetup->pm_status == POSTMASTER_STATUS_READY;
	}

	/*
	 * Ok so we have a postmaster.pid file with a pid > 0 (not a standalone
	 * backend, the service has started). Postgres might still be "starting"
	 * rather than "ready" though, so let's continue our attempts and make sure
	 * that Postgres is ready.
	 */
	for (; !pgIsReady; attempts++)
	{
		uint64_t now = time(NULL);

		pgIsReady = pg_setup_is_ready(pgSetup, postgresNotRunningIsOk);

		/* let's not be THAT verbose about it */
		if ((attempts - 1) % 10 == 0)
		{
			log_debug("pg_setup_wait_until_is_ready(): pgstatus is %s, "
					  "pid %d (was %d), after %ds and %d attempt(s)",
					  pmStatusToString(pgSetup->pm_status),
					  pgSetup->pidFile.pid,
					  previousPostgresPid,
					  (int) (now - startTime),
					  attempts);
		}

		/* we're done if we reach the timeout */
		if ((now - startTime) >= timeout)
		{
			break;
		}

		/* sleep 100 ms in between postmaster.pid probes */
		pg_usleep(100 * 1000);
	}

	if (!pgIsReady)
	{
		/* offer more diagnostic information to the user */
		postgresNotRunningIsOk = false;
		pgIsReady = pg_setup_is_ready(pgSetup, postgresNotRunningIsOk);

		log_trace("pg_setup_wait_until_is_ready returns %s [%s]",
				  pgIsReady ? "true" : "false",
				  pmStatusToString(pgSetup->pm_status));

		return pgIsReady;
	}

	/* here we know that pgIsReady is true */
	log_level(logLevel,
			  "Postgres is now serving PGDATA \"%s\" on port %d with pid %d",
			  pgSetup->pgdata, pgSetup->pgport, pgSetup->pidFile.pid);
	return true;
}


/*
 * pg_setup_wait_until_is_stopped loops over pg_ctl_status() and returns when
 * Postgres is stopped. The loop tries every 100ms up to the given timeout,
 * given in seconds.
 */
bool
pg_setup_wait_until_is_stopped(PostgresSetup *pgSetup, int timeout, int logLevel)
{
	uint64_t startTime = time(NULL);
	int attempts = 0;
	int status = -1;

	pid_t previousPostgresPid = pgSetup->pidFile.pid;

	bool missingPgdataIsOk = false;
	bool postgresNotRunningIsOk = true;

	for (attempts = 1; status != PG_CTL_STATUS_NOT_RUNNING; attempts++)
	{
		uint64_t now = time(NULL);

		/*
		 * If we don't have a postmaster.pid consider that Postgres is not
		 * running.
		 */
		if (!get_pgpid(pgSetup, postgresNotRunningIsOk))
		{
			return true;
		}

		/* we don't log the output for pg_ctl_status here */
		status = pg_ctl_status(pgSetup->pg_ctl, pgSetup->pgdata, false);

		log_trace("keeper_update_postgres_expected_status(): "
				  "pg_ctl status is %d (we expect %d: not running), "
				  "after %ds and %d attempt(s)",
				  status,
				  PG_CTL_STATUS_NOT_RUNNING,
				  (int) (now - startTime),
				  attempts);

		if (status == PG_CTL_STATUS_NOT_RUNNING)
		{
			return true;
		}

		/* we're done if we reach the timeout */
		if ((now - startTime) >= timeout)
		{
			break;
		}

		/* wait for 100 ms and try again */
		pg_usleep(100 * 1000);
	}

	/* update settings from running database */
	if (previousPostgresPid != pgSetup->pidFile.pid)
	{
		/*
		 * Update our pgSetup view of Postgres once we have made sure it's
		 * running.
		 */
		PostgresSetup newPgSetup = { 0 };

		if (!pg_setup_init(&newPgSetup,
						   pgSetup,
						   missingPgdataIsOk,
						   postgresNotRunningIsOk))
		{
			/* errors have already been logged */
			return false;
		}

		*pgSetup = newPgSetup;

		log_level(logLevel,
				  "Postgres is now stopped for PGDATA \"%s\"",
				  pgSetup->pgdata);
	}

	return status == PG_CTL_STATUS_NOT_RUNNING;
}


/*
 * pg_setup_role returns an enum value representing which role the local
 * PostgreSQL instance currently has. We detect primary and secondary when
 * Postgres is running, and either recovery or unknown when Postgres is not
 * running.
 */
PostgresRole
pg_setup_role(PostgresSetup *pgSetup)
{
	char *pgdata = pgSetup->pgdata;

	if (pg_setup_is_running(pgSetup))
	{
		/*
		 * Here we have either a recovery or a standby node. We don't know for
		 * sure with just that piece of information.
		 *
		 * If we are using Postgres 12+ and there's a standby.signal file in
		 * PGDATA, that's a strong hint that we can't have in previous version
		 * short of parsing recovery.conf.
		 *
		 * Remember that in versions before Postgres 12 the standby_mode was
		 * not exposed as a GUC so we can't inquire about that either. We would
		 * have to parse the recovery.conf file for getting the standby mode.
		 *
		 * It's easier to just return POSTGRES_ROLE_RECOVERY in that case, and
		 * let the caller figure out that this might be POSTGRES_ROLE_STANDBY.
		 * At the moment the callers don't need that level of detail anyway.
		 */
		if (pgSetup->is_in_recovery)
		{
			char recoverySignalPath[MAXPGPATH] = { 0 };

			join_path_components(recoverySignalPath, pgdata, "standby.signal");

			if (file_exists(recoverySignalPath))
			{
				return POSTGRES_ROLE_STANDBY;
			}
			else
			{
				/* We are in recovery, we don't know if we are a standby */
				return POSTGRES_ROLE_RECOVERY;
			}
		}

		/*
		 * Here it's running and SELECT pg_is_in_recovery() is false, so we
		 * know we are talking about a primary server.
		 */
		else
		{
			return POSTGRES_ROLE_PRIMARY;
		}
	}
	else
	{
		/*
		 * PostgreSQL is not running, we don't know yet... what we know is that
		 * to be a standby the file $PDGATA/recovery.conf needs to be setup (up
		 * to version 11 included), or the file $PGDATA/standby.signal needs to
		 * exists (starting with version 12). A recovery.signal file starting
		 * in Postgres 12 also indicates that we're not a primary server.
		 *
		 * There's no way that a Postgres instance is going to be a recovery or
		 * standby node without one of those files existing:
		 */
		char standbyFilesArray[][MAXPGPATH] = {
			"recovery.conf",
			"recovery.signal",
			"standby.signal"
		};
		PostgresRole standbyRoleArray[] = {
			/* default to recovery, might be a standby */
			POSTGRES_ROLE_RECOVERY, /* recovery.conf */
			POSTGRES_ROLE_RECOVERY, /* recovery.signal */
			POSTGRES_ROLE_STANDBY   /* standby.signal */
		};
		int pos = 0, count = 3;

		for (pos = 0; pos < count; pos++)
		{
			char filePath[MAXPGPATH] = { 0 };

			join_path_components(filePath, pgdata, standbyFilesArray[pos]);

			if (file_exists(filePath))
			{
				return standbyRoleArray[pos];
			}
		}

		/*
		 * Postgres is not running, and there's no file around in PGDATA that
		 * allows us to have a strong opinion on whether this instance is a
		 * primary or a standby. It might be either.
		 */
		return POSTGRES_ROLE_UNKNOWN;
	}

	return POSTGRES_ROLE_UNKNOWN;
}


/*
 * pg_setup_get_username returns pgSetup->username when it exists, otherwise it
 * looksup the username in passwd. Lastly it fallsback to the USER environment
 * variable. When nothing works it returns DEFAULT_USERNAME PGUSER is only used
 * when creating our configuration for the first time.
 */
char *
pg_setup_get_username(PostgresSetup *pgSetup)
{
	char userEnv[NAMEDATALEN] = { 0 };

	/* use a configured username if provided */
	if (!IS_EMPTY_STRING_BUFFER(pgSetup->username))
	{
		return pgSetup->username;
	}

	log_trace("username not configured");

	/* use the passwd file to find the username, same as whoami */
	uid_t uid = geteuid();
	struct passwd *pw = getpwuid(uid);
	if (pw)
	{
		log_trace("username found in passwd: %s", pw->pw_name);

		strlcpy(pgSetup->username, pw->pw_name, sizeof(pgSetup->username));
		return pgSetup->username;
	}


	/* fallback on USER from env if the user cannot be found in passwd */
	if (get_env_copy("USER", userEnv, NAMEDATALEN))
	{
		log_trace("username found in USER environment variable: %s", userEnv);

		strlcpy(pgSetup->username, userEnv, sizeof(pgSetup->username));
		return pgSetup->username;
	}

	log_trace("username fallback to default: %s", DEFAULT_USERNAME);
	strlcpy(pgSetup->username, DEFAULT_USERNAME, sizeof(pgSetup->username));

	return pgSetup->username;
}


/*
 * pg_setup_get_auth_method returns pgSetup->authMethod when it exists,
 * otherwise it returns DEFAULT_AUTH_METHOD
 */
char *
pg_setup_get_auth_method(PostgresSetup *pgSetup)
{
	if (!IS_EMPTY_STRING_BUFFER(pgSetup->authMethod))
	{
		return pgSetup->authMethod;
	}

	log_trace("auth method not configured, falling back to default value : %s",
			  DEFAULT_AUTH_METHOD);

	return DEFAULT_AUTH_METHOD;
}


/*
 * pg_setup_skip_hba_edits returns true when the user had setup pg_autoctl to
 * skip editing HBA entries.
 */
bool
pg_setup_skip_hba_edits(PostgresSetup *pgSetup)
{
	return pgSetup->hbaLevel == HBA_EDIT_SKIP;
}


/*
 * pg_setup_set_absolute_pgdata uses realpath(3) to make sure that we re using
 * the absolute real pathname for PGDATA in our setup, so that services will
 * work correctly after keeper/monitor init, even when initializing in a
 * relative path and starting the service from elsewhere. This function returns
 * true if the pgdata path has been updated in the setup.
 */
bool
pg_setup_set_absolute_pgdata(PostgresSetup *pgSetup)
{
	return normalize_filename(pgSetup->pgdata, pgSetup->pgdata, MAXPGPATH);
}


/*
 * nodeKindFromString returns a PgInstanceKind from a string.
 */
PgInstanceKind
nodeKindFromString(const char *nodeKind)
{
	PgInstanceKind kindArray[] = {
		NODE_KIND_UNKNOWN,
		NODE_KIND_UNKNOWN,
		NODE_KIND_STANDALONE,
		NODE_KIND_CITUS_COORDINATOR,
		NODE_KIND_CITUS_WORKER
	};
	char *kindList[] = {
		"", "unknown", "standalone", "coordinator", "worker", NULL
	};

	for (int listIndex = 0; kindList[listIndex] != NULL; listIndex++)
	{
		char *candidate = kindList[listIndex];

		if (strcmp(nodeKind, candidate) == 0)
		{
			PgInstanceKind pgKind = kindArray[listIndex];
			log_trace("nodeKindFromString: \"%s\" ➜ %d", nodeKind, pgKind);
			return pgKind;
		}
	}

	log_fatal("Failed to parse nodeKind \"%s\"", nodeKind);

	/* never happens, make compiler happy */
	return NODE_KIND_UNKNOWN;
}


/*
 * nodeKindToString returns a textual representatin of given PgInstanceKind.
 * This must be kept in sync with src/monitor/formation_metadata.c function
 * FormationKindFromNodeKindString.
 */
char *
nodeKindToString(PgInstanceKind kind)
{
	switch (kind)
	{
		case NODE_KIND_STANDALONE:
		{
			return "standalone";
		}

		case NODE_KIND_CITUS_COORDINATOR:
		{
			return "coordinator";
		}

		case NODE_KIND_CITUS_WORKER:
		{
			return "worker";
		}

		default:
			log_fatal("nodeKindToString: unknown node kind %d", kind);
			return NULL;
	}

	/* can't happen, keep compiler happy */
	return NULL;
}


/*
 * pmStatusFromString parses the Postgres postmaster.pid PM_STATUS line into
 * our own enum to represent the value.
 */
static PostmasterStatus
pmStatusFromString(const char *postmasterStatus)
{
	if (strcmp(postmasterStatus, PM_STATUS_STARTING) == 0)
	{
		return POSTMASTER_STATUS_STARTING;
	}
	else if (strcmp(postmasterStatus, PM_STATUS_STOPPING) == 0)
	{
		return POSTMASTER_STATUS_STOPPING;
	}
	else if (strcmp(postmasterStatus, PM_STATUS_READY) == 0)
	{
		return POSTMASTER_STATUS_READY;
	}
	else if (strcmp(postmasterStatus, PM_STATUS_STANDBY) == 0)
	{
		return POSTMASTER_STATUS_STANDBY;
	}

	log_warn("Failed to read Postmaster status: \"%s\"", postmasterStatus);
	return POSTMASTER_STATUS_UNKNOWN;
}


/*
 * pmStatusToString returns a textual representation of given Postmaster status
 * given as a PmStatus enum.
 *
 * We're not using the PM_STATUS_READY etc constants here because those are
 * blank-padded to always be the same length, and then the warning messages
 * including "ready " look buggy in a way.
 */
char *
pmStatusToString(PostmasterStatus pm_status)
{
	switch (pm_status)
	{
		case POSTMASTER_STATUS_UNKNOWN:
		{
			return "unknown";
		}

		case POSTMASTER_STATUS_STARTING:
		{
			return "starting";
		}

		case POSTMASTER_STATUS_STOPPING:
		{
			return "stopping";
		}

		case POSTMASTER_STATUS_READY:
		{
			return "ready";
		}

		case POSTMASTER_STATUS_STANDBY:
			return "standby";
	}

	/* keep compiler happy */
	return "unknown";
}


/*
 * pgsetup_get_pgport returns the port to use either from the PGPORT
 * environment variable, or from our default hard-coded value of 5432.
 */
int
pgsetup_get_pgport()
{
	char pgport_env[NAMEDATALEN];
	int pgport = 0;

	if (env_exists("PGPORT") && get_env_copy("PGPORT", pgport_env, NAMEDATALEN))
	{
		if (stringToInt(pgport_env, &pgport) && pgport > 0)
		{
			return pgport;
		}
		else
		{
			log_warn("Failed to parse PGPORT value \"%s\", using %d",
					 pgport_env, POSTGRES_PORT);
			return POSTGRES_PORT;
		}
	}
	else
	{
		/* no PGPORT */
		return POSTGRES_PORT;
	}
}


/*
 * pgsetup_validate_ssl_settings returns true if our SSL settings are following
 * one of the three following cases:
 *
 *  - --no-ssl:          ssl is not activated and no file has been provided
 *  - --ssl-self-signed: ssl is activated and no file has been provided
 *  - --ssl-*-files:     ssl is activated and all the files have been provided
 *
 * Otherwise it logs an error message and return false.
 */
bool
pgsetup_validate_ssl_settings(PostgresSetup *pgSetup)
{
	SSLOptions *ssl = &(pgSetup->ssl);

	log_trace("pgsetup_validate_ssl_settings");

	/*
	 * When using the full SSL options, we validate that the files exists where
	 * given and set the default sslmode to verify-full.
	 *
	 *  --ssl-ca-file
	 *  --ssl-crl-file
	 *  --server-cert
	 *  --server-key
	 */
	if (ssl->active && !ssl->createSelfSignedCert)
	{
		/*
		 * When passing files in manually for SSL we need at least  cert and a
		 * key
		 */
		if (IS_EMPTY_STRING_BUFFER(ssl->serverCert) ||
			IS_EMPTY_STRING_BUFFER(ssl->serverKey))
		{
			log_error("Failed to setup SSL with user-provided certificates: "
					  "options --server-cert and --server-key are required.");
			return false;
		}

		/* check that the given files exist */
		if (!file_exists(ssl->serverCert))
		{
			log_error("--server-cert file does not exist at \"%s\"",
					  ssl->serverCert);
			return false;
		}

		if (!file_exists(ssl->serverKey))
		{
			log_error("--server-key file does not exist at \"%s\"",
					  ssl->serverKey);
			return false;
		}

		if (!IS_EMPTY_STRING_BUFFER(ssl->caFile) && !file_exists(ssl->caFile))
		{
			log_error("--ssl-ca-file file does not exist at \"%s\"",
					  ssl->caFile);
			return false;
		}

		if (!IS_EMPTY_STRING_BUFFER(ssl->crlFile) && !file_exists(ssl->crlFile))
		{
			log_error("--ssl-crl-file file does not exist at \"%s\"",
					  ssl->crlFile);
			return false;
		}

		/* install a default value for --ssl-mode, use verify-full */
		if (ssl->sslMode == SSL_MODE_UNKNOWN)
		{
			ssl->sslMode = SSL_MODE_VERIFY_FULL;
			strlcpy(ssl->sslModeStr,
					pgsetup_sslmode_to_string(ssl->sslMode), SSL_MODE_STRLEN);
			log_info("Using default --ssl-mode \"%s\"", ssl->sslModeStr);
		}

		/* check that we have a CA file to use with verif-ca/verify-full */
		if (ssl->sslMode >= SSL_MODE_VERIFY_CA && IS_EMPTY_STRING_BUFFER(ssl->caFile))
		{
			log_error("--ssl-ca-file is required when --ssl-mode \"%s\" is used",
					  ssl->sslModeStr);
			return false;
		}

		/*
		 * Normalize the filenames.
		 * We already log errors so we can simply return the result
		 */
		return normalize_filename(pgSetup->ssl.caFile, pgSetup->ssl.caFile,
								  MAXPGPATH) &&
			   normalize_filename(pgSetup->ssl.crlFile, pgSetup->ssl.crlFile,
								  MAXPGPATH) &&
			   normalize_filename(pgSetup->ssl.serverCert, pgSetup->ssl.serverCert,
								  MAXPGPATH) &&
			   normalize_filename(pgSetup->ssl.serverKey, pgSetup->ssl.serverKey,
								  MAXPGPATH);
	}

	/*
	 * When --ssl-self-signed is used, we default to using sslmode=require.
	 * Setting higher than that are wrong, false sense of security.
	 */
	if (ssl->createSelfSignedCert)
	{
		/* in that case we want an sslMode of require at most */
		if (ssl->sslMode > SSL_MODE_REQUIRE)
		{
			log_error("--ssl-mode \"%s\" is not compatible with self-signed "
					  "certificates, please provide certificates signed by "
					  "your trusted CA.",
					  pgsetup_sslmode_to_string(ssl->sslMode));
			log_info("See https://www.postgresql.org/docs/current/libpq-ssl.html"
					 " for details");
			return false;
		}

		if (ssl->sslMode == SSL_MODE_UNKNOWN)
		{
			/* install a default value for --ssl-mode */
			ssl->sslMode = SSL_MODE_REQUIRE;
			strlcpy(ssl->sslModeStr,
					pgsetup_sslmode_to_string(ssl->sslMode), SSL_MODE_STRLEN);
			log_info("Using default --ssl-mode \"%s\"", ssl->sslModeStr);
		}

		log_info("Using --ssl-self-signed: pg_autoctl will "
				 "create self-signed certificates, allowing for "
				 "encrypted network traffic");
		log_warn("Self-signed certificates provide protection against "
				 "eavesdropping; this setup does NOT protect against "
				 "Man-In-The-Middle attacks nor Impersonation attacks.");
		log_warn("See https://www.postgresql.org/docs/current/libpq-ssl.html "
				 "for details");

		return true;
	}

	/* --no-ssl is ok */
	if (ssl->active == 0)
	{
		log_warn("No encryption is used for network traffic! This allows an "
				 "attacker on the network to read all replication data.");
		log_warn("Using --ssl-self-signed instead of --no-ssl is recommend to "
				 "achieve more security with the same ease of deployment.");
		log_warn("See https://www.postgresql.org/docs/current/libpq-ssl.html "
				 "for details on how to improve");

		/* Install a default value for --ssl-mode */
		if (ssl->sslMode == SSL_MODE_UNKNOWN)
		{
			ssl->sslMode = SSL_MODE_PREFER;
			strlcpy(ssl->sslModeStr,
					pgsetup_sslmode_to_string(ssl->sslMode), SSL_MODE_STRLEN);
			log_info("Using default --ssl-mode \"%s\"", ssl->sslModeStr);
		}
		return true;
	}

	return false;
}


/*
 * pg_setup_sslmode_to_string parses a string representing the sslmode into an
 * internal enum value, so that we can easily compare values.
 */
SSLMode
pgsetup_parse_sslmode(const char *sslMode)
{
	SSLMode enumArray[] = {
		SSL_MODE_DISABLE,
		SSL_MODE_ALLOW,
		SSL_MODE_PREFER,
		SSL_MODE_REQUIRE,
		SSL_MODE_VERIFY_CA,
		SSL_MODE_VERIFY_FULL
	};

	char *sslModeArray[] = {
		"disable", "allow", "prefer", "require",
		"verify-ca", "verify-full", NULL
	};

	int sslModeArrayIndex = 0;

	for (sslModeArrayIndex = 0;
		 sslModeArray[sslModeArrayIndex] != NULL;
		 sslModeArrayIndex++)
	{
		if (strcmp(sslMode, sslModeArray[sslModeArrayIndex]) == 0)
		{
			return enumArray[sslModeArrayIndex];
		}
	}

	return SSL_MODE_UNKNOWN;
}


/*
 * pgsetup_sslmode_to_string returns the string representation of the enum.
 */
char *
pgsetup_sslmode_to_string(SSLMode sslMode)
{
	switch (sslMode)
	{
		case SSL_MODE_UNKNOWN:
		{
			return "unknown";
		}

		case SSL_MODE_DISABLE:
		{
			return "disable";
		}

		case SSL_MODE_ALLOW:
		{
			return "allow";
		}

		case SSL_MODE_PREFER:
		{
			return "prefer";
		}

		case SSL_MODE_REQUIRE:
		{
			return "require";
		}

		case SSL_MODE_VERIFY_CA:
		{
			return "verify-ca";
		}

		case SSL_MODE_VERIFY_FULL:
			return "verify-full";
	}

	/* This is a huge bug */
	log_error("BUG: some unknown SSL_MODE enum value was encountered");
	return "unknown";
}


/*
 * pg_setup_standby_slot_supported returns true when the target Postgres
 * instance represented in pgSetup is compatible with using
 * pg_replication_slot_advance() on a standby node.
 *
 * In Postgres 11 and 12, the pg_replication_slot_advance() function has been
 * buggy and prevented WAL recycling on standby nodes.
 *
 * See https://github.com/citusdata/pg_auto_failover/issues/283 for the problem
 * and https://git.postgresql.org/gitweb/?p=postgresql.git;a=commit;h=b48df81
 * for the solution.
 *
 * We need Postgres 11 starting at 11.9, Postgres 12 starting at 12.4, or
 * Postgres 13 or more recent to make use of pg_replication_slot_advance.
 */
bool
pg_setup_standby_slot_supported(PostgresSetup *pgSetup, int logLevel)
{
	int pg_version = 0;

	if (!parse_pg_version_string(pgSetup->pg_version, &pg_version))
	{
		/* errors have already been logged */
		return false;
	}

	int major = pg_version / 100;
	int minor = pg_version % 100;

	/* do we have Postgres 10 (or before, though we don't support that) */
	if (pg_version < 1100)
	{
		log_trace("pg_setup_standby_slot_supported(%d): false", pg_version);
		return false;
	}

	/* Postgres 11.0 up to 11.8 included the bug */
	if (pg_version >= 1100 && pg_version < 1109)
	{
		log_level(logLevel,
				  "Postgres %d.%d does not support replication slots "
				  "on a standby node", major, minor);

		return false;
	}

	/* Postgres 11.9 and up are good */
	if (pg_version >= 1109 && pg_version < 1200)
	{
		return true;
	}

	/* Postgres 12.0 up to 12.3 included the bug */
	if (pg_version >= 1200 && pg_version < 1204)
	{
		log_level(logLevel,
				  "Postgres %d.%d does not support replication slots "
				  "on a standby node", major, minor);

		return false;
	}

	/* Postgres 12.4 and up are good */
	if (pg_version >= 1204 && pg_version < 1300)
	{
		return true;
	}

	/* Starting with Postgres 13, all versions are known to have the bug fix */
	if (pg_version >= 1300)
	{
		return true;
	}

	/* should not happen */
	log_debug("BUG in pg_setup_standby_slot_supported(%d): "
			  "unknown Postgres version, returning false",
			  pg_version);

	return false;
}


/*
 * pgsetup_parse_hba_level parses a string that represents an HBAEditLevel
 * value.
 */
HBAEditLevel
pgsetup_parse_hba_level(const char *level)
{
	HBAEditLevel enumArray[] = {
		HBA_EDIT_SKIP,
		HBA_EDIT_MINIMAL,
		HBA_EDIT_LAN
	};

	char *levelArray[] = { "skip", "minimal", "app", NULL };

	for (int i = 0; levelArray[i] != NULL; i++)
	{
		if (strcmp(level, levelArray[i]) == 0)
		{
			return enumArray[i];
		}
	}

	return HBA_EDIT_UNKNOWN;
}


/*
 * pgsetup_hba_level_to_string returns the string representation of an
 * hbaLevel enum value.
 */
char *
pgsetup_hba_level_to_string(HBAEditLevel hbaLevel)
{
	switch (hbaLevel)
	{
		case HBA_EDIT_SKIP:
		{
			return "skip";
		}

		case HBA_EDIT_MINIMAL:
		{
			return "minimal";
		}

		case HBA_EDIT_LAN:
		{
			return "app";
		}

		case HBA_EDIT_UNKNOWN:
			return "unknown";
	}

	log_error("BUG: hbaLevel %d is unknown", hbaLevel);
	return "unknown";
}


/*
 * dbstateToString returns a string from a pgControlFile state enum.
 */
const char *
dbstateToString(DBState state)
{
	switch (state)
	{
		case DB_STARTUP:
		{
			return "starting up";
		}

		case DB_SHUTDOWNED:
		{
			return "shut down";
		}

		case DB_SHUTDOWNED_IN_RECOVERY:
		{
			return "shut down in recovery";
		}

		case DB_SHUTDOWNING:
		{
			return "shutting down";
		}

		case DB_IN_CRASH_RECOVERY:
		{
			return "in crash recovery";
		}

		case DB_IN_ARCHIVE_RECOVERY:
		{
			return "in archive recovery";
		}

		case DB_IN_PRODUCTION:
			return "in production";
	}
	return "unrecognized status code";
}
