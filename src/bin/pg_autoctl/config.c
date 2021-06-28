/*
 * src/bin/pg_autoctl/config.c
 *     Common configuration functions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdbool.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "config.h"
#include "defaults.h"
#include "env_utils.h"
#include "file_utils.h"
#include "ini_file.h"
#include "keeper.h"
#include "keeper_config.h"
#include "log.h"
#include "pgctl.h"


/*
 * build_xdg_path is an helper function that builds the full path to an XDG
 * compatible resource: either a configuration file, a runtime file, or a data
 * file.
 */
bool
build_xdg_path(char *dst,
			   XDGResourceType xdgType,
			   const char *pgdata,
			   const char *name)
{
	char filename[MAXPGPATH];
	char home[MAXPGPATH];
	char fallback[MAXPGPATH];
	char xdg_topdir[MAXPGPATH];
	char *envVarName = NULL;

	if (!get_env_copy("HOME", home, MAXPGPATH))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	switch (xdgType)
	{
		case XDG_DATA:
		{
			join_path_components(fallback, home, ".local/share");
			envVarName = "XDG_DATA_HOME";
			break;
		}

		case XDG_CONFIG:
		{
			join_path_components(fallback, home, ".config");
			envVarName = "XDG_CONFIG_HOME";
			break;
		}

		case XDG_RUNTIME:
		{
			strlcpy(fallback, "/tmp", MAXPGPATH);
			envVarName = "XDG_RUNTIME_DIR";
			break;
		}

		default:

			/* developper error */
			log_error("No support for XDG Resource Type %d", xdgType);
			return false;
	}

	if (!get_env_copy_with_fallback(envVarName, xdg_topdir, MAXPGPATH, fallback))
	{
		/* errors have already been logged */
		return false;
	}

	if (xdgType == XDG_RUNTIME && !directory_exists(xdg_topdir))
	{
		strlcpy(xdg_topdir, "/tmp", MAXPGPATH);
	}

	join_path_components(filename, xdg_topdir, "pg_autoctl");

	/* append PGDATA now */
	if (pgdata[0] == '/')
	{
		/* skip the first / to avoid having a double-slash in the name */
		join_path_components(filename, filename, pgdata + 1);
	}
	else
	{
		/*
		 * We have a relative pathname to PGDATA, and we want an absolute
		 * pathname in our configuration directory name so that we make
		 * sure to find it again.
		 *
		 * It could be that the PGDATA directory we are given doesn't exist
		 * yet, precluding the use of realpath(3) to get the absolute name
		 * here.
		 */
		char currentWorkingDirectory[MAXPGPATH] = { 0 };

		if (getcwd(currentWorkingDirectory, MAXPGPATH) == NULL)
		{
			log_error("Failed to get the current working directory: %m");
			return false;
		}

		/* avoid double-slash by skipping the first one */
		join_path_components(filename, filename, currentWorkingDirectory + 1);

		/* now add in pgdata */
		join_path_components(filename, filename, pgdata);
	}

	/* mkdir -p the target directory */
	if (pg_mkdir_p(filename, 0755) == -1)
	{
		log_error("Failed to create state directory \"%s\": %m", filename);
		return false;
	}

	/* normalize the existing path to the configuration file */
	if (!normalize_filename(filename, dst, MAXPGPATH))
	{
		/* errors have already been logged */
		return false;
	}

	/* and finally add the configuration file name */
	join_path_components(dst, dst, name);

	return true;
}


/*
 * SetConfigFilePath sets config.pathnames.config from config.pgSetup.pgdata,
 * which must have been set previously.
 */
bool
SetConfigFilePath(ConfigFilePaths *pathnames, const char *pgdata)
{
	/* don't overwrite already computed value */
	if (IS_EMPTY_STRING_BUFFER(pathnames->config))
	{
		if (!build_xdg_path(pathnames->config,
							XDG_CONFIG,
							pgdata,
							KEEPER_CONFIGURATION_FILENAME))
		{
			log_error("Failed to build our configuration file pathname, "
					  "see above.");
			return false;
		}
	}

	log_trace("SetConfigFilePath: \"%s\"", pathnames->config);

	return true;
}


/*
 * SetStateFilePath sets config.pathnames.state from our PGDATA value, and
 * using the XDG Base Directory Specification for a data file. Per specs at:
 *
 *   https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
 *
 */
bool
SetStateFilePath(ConfigFilePaths *pathnames, const char *pgdata)
{
	if (IS_EMPTY_STRING_BUFFER(pathnames->state))
	{
		if (!build_xdg_path(pathnames->state,
							XDG_DATA,
							pgdata,
							KEEPER_STATE_FILENAME))
		{
			log_error("Failed to build pg_autoctl state file pathname, "
					  "see above.");
			return false;
		}
	}
	log_trace("SetStateFilePath: \"%s\"", pathnames->state);

	/* now the init state file */
	if (IS_EMPTY_STRING_BUFFER(pathnames->init))
	{
		if (!build_xdg_path(pathnames->init,
							XDG_DATA,
							pgdata,
							KEEPER_INIT_STATE_FILENAME))
		{
			log_error("Failed to build pg_autoctl init state file pathname, "
					  "see above.");
			return false;
		}
	}
	log_trace("SetKeeperStateFilePath: \"%s\"", pathnames->init);

	return true;
}


/*
 * SetNodesFilePath sets config.pathnames.nodes from our PGDATA value, and
 * using the XDG Base Directory Specification for a data file. Per specs at:
 *
 *   https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
 *
 */
bool
SetNodesFilePath(ConfigFilePaths *pathnames, const char *pgdata)
{
	if (IS_EMPTY_STRING_BUFFER(pathnames->nodes))
	{
		if (!build_xdg_path(pathnames->nodes,
							XDG_DATA,
							pgdata,
							KEEPER_NODES_FILENAME))
		{
			log_error("Failed to build pg_autoctl state file pathname, "
					  "see above.");
			return false;
		}
	}
	log_trace("SetNodesFilePath: \"%s\"", pathnames->nodes);

	return true;
}


/*
 * SetPidFilePath sets config.pathnames.pidfile from our PGDATA value, and
 * using the XDG Base Directory Specification for a runtime file.
 */
bool
SetPidFilePath(ConfigFilePaths *pathnames, const char *pgdata)
{
	if (IS_EMPTY_STRING_BUFFER(pathnames->pid))
	{
		if (!build_xdg_path(pathnames->pid,
							XDG_RUNTIME,
							pgdata,
							KEEPER_PID_FILENAME))
		{
			log_error("Failed to build pg_autoctl pid file pathname, "
					  "see above.");
			return false;
		}
	}

	log_trace("SetPidFilePath: \"%s\"", pathnames->pid);

	return true;
}


/*
 * ProbeConfigurationFileRole opens a configuration file at given filename and
 * probes the pg_autoctl role it belongs to: either a monitor or a keeper.
 *
 * We use a IniOption array with a single entry here, the pg_autoctl.role
 * setting that indicates which role is our configuration file intended to be
 * read as: either "monitor" or "keeper".
 */
pgAutoCtlNodeRole
ProbeConfigurationFileRole(const char *filename)
{
	MinimalConfig config = { 0 };
	IniOption configOptions[] = {
		make_strbuf_option("pg_autoctl", "role",
						   NULL, true, NAMEDATALEN, config.role),
		INI_OPTION_LAST
	};

	log_debug("Probing configuration file \"%s\"", filename);

	/*
	 * There is a race condition at process startup where a configuration file
	 * can disappear while being overwritten. Reduce the chances of that
	 * happening by making more than one attempt at reading the file.
	 */
	char *fileContents = NULL;

	for (int attempts = 0; fileContents == NULL && attempts < 3; attempts++)
	{
		long fileSize = 0L;

		if (read_file_if_exists(filename, &fileContents, &fileSize))
		{
			break;
		}

		pg_usleep(100 * 1000);  /* 100ms */
	}

	if (fileContents == NULL)
	{
		log_error("Failed to read configuration file \"%s\"", filename);
		return PG_AUTOCTL_ROLE_UNKNOWN;
	}

	if (!parse_ini_buffer(filename, fileContents, configOptions))
	{
		log_error("Failed to parse configuration file \"%s\"", filename);
		return PG_AUTOCTL_ROLE_UNKNOWN;
	}

	log_debug("ProbeConfigurationFileRole: %s", config.role);

	if (strcmp(config.role, MONITOR_ROLE) == 0)
	{
		return PG_AUTOCTL_ROLE_MONITOR;
	}
	else if (strcmp(config.role, KEEPER_ROLE) == 0)
	{
		return PG_AUTOCTL_ROLE_KEEPER;
	}
	else
	{
		log_fatal("Failed to recognize configuration file setting for "
				  "pg_autoctl.role: \"%s\"", config.role);

		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* can't happen: keep compiler happy */
	return PG_AUTOCTL_ROLE_UNKNOWN;
}


/*
 * config_accept_new_ssloptions allows to reload SSL options at runtime.
 */
bool
config_accept_new_ssloptions(PostgresSetup *pgSetup, PostgresSetup *newPgSetup)
{
	if (pgSetup->ssl.active != newPgSetup->ssl.active)
	{
		log_info("Reloading configuration: ssl is now %s; used to be %s",
				 newPgSetup->ssl.active ? "active" : "disabled",
				 pgSetup->ssl.active ? "active" : "disabled");
	}

	if (pgSetup->ssl.sslMode != newPgSetup->ssl.sslMode)
	{
		log_info("Reloading configuration: sslmode is now \"%s\"; "
				 "used to be \"%s\"",
				 pgsetup_sslmode_to_string(newPgSetup->ssl.sslMode),
				 pgsetup_sslmode_to_string(pgSetup->ssl.sslMode));
	}

	if (strneq(pgSetup->ssl.caFile, newPgSetup->ssl.caFile))
	{
		log_info("Reloading configuration: ssl CA file is now \"%s\"; "
				 "used to be \"%s\"",
				 newPgSetup->ssl.caFile, pgSetup->ssl.caFile);
	}

	if (strneq(pgSetup->ssl.crlFile, newPgSetup->ssl.crlFile))
	{
		log_info("Reloading configuration: ssl CRL file is now \"%s\"; "
				 "used to be \"%s\"",
				 newPgSetup->ssl.crlFile, pgSetup->ssl.crlFile);
	}

	if (strneq(pgSetup->ssl.serverCert, newPgSetup->ssl.serverCert))
	{
		log_info("Reloading configuration: ssl server cert file is now \"%s\"; "
				 "used to be \"%s\"",
				 newPgSetup->ssl.serverCert,
				 pgSetup->ssl.serverCert);
	}

	if (strneq(pgSetup->ssl.serverKey, newPgSetup->ssl.serverKey))
	{
		log_info("Reloading configuration: ssl server key file is now \"%s\"; "
				 "used to be \"%s\"",
				 newPgSetup->ssl.serverKey,
				 pgSetup->ssl.serverKey);
	}

	/* install the new SSL settings, wholesale */
	pgSetup->ssl = newPgSetup->ssl;
	strlcpy(pgSetup->ssl.sslModeStr,
			pgsetup_sslmode_to_string(pgSetup->ssl.sslMode),
			SSL_MODE_STRLEN);

	return true;
}
