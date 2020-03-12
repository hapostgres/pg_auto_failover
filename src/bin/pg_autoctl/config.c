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
	char *home = getenv("HOME");
	char *xdg_topdir;

	if (home == NULL)
	{
		log_fatal("Environment variable HOME is unset");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	switch (xdgType)
	{
		case XDG_DATA:
		{
			xdg_topdir = getenv("XDG_DATA_HOME");
			break;
		}

		case XDG_CONFIG:
		{
			xdg_topdir = getenv("XDG_CONFIG_HOME");
			break;
		}

		case XDG_RUNTIME:
		{
			xdg_topdir = getenv("XDG_RUNTIME_DIR");

			if (xdg_topdir == NULL || !directory_exists(xdg_topdir))
			{
				/* then default to /tmp */
				xdg_topdir = "/tmp";
			}
			break;
		}

		default:

			/* developper error */
			log_error("No support for XDG Resource Type %d", xdgType);
			return false;
	}

	if (xdg_topdir != NULL)
	{
		/* use e.g. ${XDG_DATA_HOME}/pg_autoctl/<PGDATA>/pg_autoctl.state */
		strlcpy(filename, xdg_topdir, MAXPGPATH);
	}
	else
	{
		/* use e.g. ${HOME}/.local/share/pg_autoctl/<PGDATA>/pg_autoctl.state */
		strlcpy(filename, home, MAXPGPATH);

		switch (xdgType)
		{
			case XDG_DATA:
			{
				join_path_components(filename, filename, ".local/share");
				break;
			}

			case XDG_CONFIG:
			{
				join_path_components(filename, filename, ".config");
				break;
			}

			default:

				/* can not happen given previous switch */
				log_error("No support for XDG Resource Type %d", xdgType);
				return false;
		}
	}

	join_path_components(filename, filename, "pg_autoctl");

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
		char currentWorkingDirectory[MAXPGPATH];

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

	/* and finally add the configuration file name */
	join_path_components(filename, filename, name);

	/* normalize the path to the configuration file, if it exists */
	if (!normalize_filename(filename, dst, MAXPGPATH))
	{
		/* errors have already been logged */
		return false;
	}

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
			exit(EXIT_CODE_INTERNAL_ERROR);
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
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
	log_trace("SetStateFilePath: \"%s\"", pathnames->state);

	/* now the init state file */
	if (IS_EMPTY_STRING_BUFFER(pathnames->init))
	{
		if (!build_xdg_path(pathnames->init,
							XDG_DATA,
							pgdata,
							KEEPER_INIT_FILENAME))
		{
			log_error("Failed to build pg_autoctl init state file pathname, "
					  "see above.");
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
	log_trace("SetKeeperStateFilePath: \"%s\"", pathnames->init);

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
			exit(EXIT_CODE_INTERNAL_ERROR);
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

	if (!read_ini_file(filename, configOptions))
	{
		log_fatal("Failed to parse configuration file \"%s\"", filename);
		exit(EXIT_CODE_BAD_CONFIG);
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
