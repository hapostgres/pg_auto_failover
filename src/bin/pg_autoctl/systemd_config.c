/*
 * src/bin/pg_autoctl/systemd_config.c
 *     Keeper configuration functions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdbool.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "cli_root.h"
#include "defaults.h"
#include "ini_file.h"
#include "systemd_config.h"
#include "log.h"

#include "runprogram.h"

static bool SetSystemdFilePath(ConfigFilePaths *pathnames);


#define OPTION_SYSTEMD_DESCRIPTION(config) \
	make_strbuf_option_default("Unit", "Description", NULL, true, BUFSIZE, \
							   config->Description, "pg_auto_failover")

#define OPTION_SYSTEMD_WORKING_DIRECTORY(config) \
	make_strbuf_option_default("Service", "WorkingDirectory",			\
							   NULL, true, BUFSIZE,						\
							   config->WorkingDirectory, "/var/lib/postgresql")

#define OPTION_SYSTEMD_ENVIRONMENT_PGDATA(config) \
	make_strbuf_option_default("Service", "Environment",				\
							   NULL, true, BUFSIZE,						\
							   config->EnvironmentPGDATA,				\
							   "PGDATA=/var/lib/postgresql/11/pg_auto_failover")

#define OPTION_SYSTEMD_USER(config) \
	make_strbuf_option_default("Service", "User", NULL, true, BUFSIZE,	\
							   config->User, "postgres")

#define OPTION_SYSTEMD_EXECSTART(config) \
	make_strbuf_option_default("Service", "ExecStart", NULL, true, BUFSIZE,	\
							   config->ExecStart, "/usr/bin/pg_autoctl run")

#define OPTION_SYSTEMD_RESTART(config) \
	make_strbuf_option_default("Service", "Restart", NULL, true, BUFSIZE, \
							   config->Restart, "always")

#define OPTION_SYSTEMD_STARTLIMITBURST(config) \
	make_int_option_default("Service", "StartLimitBurst", NULL, true, \
							&(config->StartLimitBurst), 20)

#define OPTION_SYSTEMD_WANTEDBY(config) \
	make_strbuf_option_default("Install", "WantedBy", NULL, true, BUFSIZE, \
							   config->WantedBy, "multi-user.target")

#define SET_INI_OPTIONS_ARRAY(config) \
	{ \
		OPTION_SYSTEMD_DESCRIPTION(config),		 \
		OPTION_SYSTEMD_WORKING_DIRECTORY(config),	\
		OPTION_SYSTEMD_ENVIRONMENT_PGDATA(config),	\
		OPTION_SYSTEMD_USER(config),				\
		OPTION_SYSTEMD_EXECSTART(config),			\
		OPTION_SYSTEMD_RESTART(config),				\
		OPTION_SYSTEMD_STARTLIMITBURST(config),		\
		OPTION_SYSTEMD_WANTEDBY(config),			\
		INI_OPTION_LAST \
	}


/*
 * systemd_config_init initialises a SystemdServiceConfig with the default
 * values.
 */
void
systemd_config_init(SystemdServiceConfig *config, const char *pgdata)
{
	IniOption systemdOptions[] = SET_INI_OPTIONS_ARRAY(config);
	char program[MAXPGPATH];

	/* time to setup config->pathnames.systemd */
	if (!SetSystemdFilePath(&(config->pathnames)))
	{
		log_fatal("Failed to set systemd filename, see above for details.");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* adjust defaults to known values from the config */
	strlcpy(config->WorkingDirectory, config->pgSetup.pgdata, MAXPGPATH);

	snprintf(config->EnvironmentPGDATA, BUFSIZE,
			 "PGDATA=%s", config->pgSetup.pgdata);

	strlcpy(config->User, config->pgSetup.username, NAMEDATALEN);

	if (!get_program_absolute_path(program, MAXPGPATH))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	snprintf(config->ExecStart, BUFSIZE, "%s run", program);

	if (!ini_validate_options(systemdOptions))
	{
		log_error("Please review your setup options per above messages");
		exit(EXIT_CODE_BAD_CONFIG);
	}
}


/*
 * keeper_config_write_file writes the current values in given KeeperConfig to
 * filename.
 */
bool
systemd_config_write_file(SystemdServiceConfig *config)
{
	const char *filePath = config->pathnames.systemd;
	bool success = false;
	FILE *fileStream = NULL;

	log_trace("systemd_config_write_file \"%s\"", filePath);

	fileStream = fopen(filePath, "w");
	if (fileStream == NULL)
	{
		log_error("Failed to open file \"%s\": %s", filePath, strerror(errno));
		return false;
	}

	success = systemd_config_write(fileStream, config);

	if (fclose(fileStream) == EOF)
	{
		log_error("Failed to write file \"%s\"", filePath);
		return false;
	}

	return success;
}


/*
 * keeper_config_write write the current config to given STREAM.
 */
bool
systemd_config_write(FILE *stream, SystemdServiceConfig *config)
{
	IniOption systemdOptions[] = SET_INI_OPTIONS_ARRAY(config);

	return write_ini_to_stream(stream, systemdOptions);
}


/*
 * systemd_enable_linger calls `loginctl enable-linger` for the current user,
 * allowing user-level services to run even when the current user is not
 * logged-in to the machine.
 */
bool
systemd_enable_linger()
{
	Program program = run_program("loginctl", "enable-linger", NULL);
	int returnCode = program.returnCode;

	log_debug("loginctl enable-linger [%d]", returnCode);

	if (program.stderr != NULL)
	{
		log_error("%s", program.stderr);
	}

	if (returnCode != 0)
	{
		/* errors have already been logged */
		free_program(&program);
		return false;
	}

	free_program(&program);
	return true;
}


/*
 * systemd_enable_linger calls `loginctl enable-linger` for the current user,
 * allowing user-level services to run even when the current user is not
 * logged-in to the machine.
 */
bool
systemd_disable_linger()
{
	Program program = run_program("loginctl", "disable-linger", NULL);
	int returnCode = program.returnCode;

	log_debug("loginctl disable-linger [%d]", returnCode);

	if (program.stderr != NULL)
	{
		log_error("%s", program.stderr);
	}

	if (returnCode != 0)
	{
		/* errors have already been logged */
		free_program(&program);
		return false;
	}

	free_program(&program);
	return true;
}


/*
 * systemd_user_daemon_reload runs the command `systemctl --user
 * daemon-reload`.
 */
bool
systemd_user_daemon_reload()
{
	Program program = run_program("systemctl", "--user", "daemon-reload", NULL);
	int returnCode = program.returnCode;

	log_debug("systemctl --user daemon-reload [%d]", returnCode);

	if (program.stderr != NULL)
	{
		log_error("%s", program.stderr);
	}

	if (returnCode != 0)
	{
		/* errors have already been logged */
		free_program(&program);
		return false;
	}

	free_program(&program);
	return true;
}


/*
 * systemd_user_daemon_reload runs the command `systemctl --user
 * daemon-reload`.
 */
bool
systemd_user_start_pgautofailover()
{
	Program program = run_program("systemctl", "--user", "start",
								  KEEPER_SYSTEMD_SERVICE,
								  NULL);
	int returnCode = program.returnCode;

	log_debug("systemctl --user start %s [%d]",
			  KEEPER_SYSTEMD_SERVICE, returnCode);

	if (program.stderr != NULL)
	{
		log_error("%s", program.stderr);
	}

	if (returnCode != 0)
	{
		/* errors have already been logged */
		free_program(&program);
		return false;
	}

	free_program(&program);
	return true;
}


/*
 * SetSystemdFilePath sets config.pathnames.systemd.
 */
static bool
SetSystemdFilePath(ConfigFilePaths *pathnames)
{
	/*
	 * As the systemd service file is to be found in ~/.config/systemd/user
	 * rather than under the ~/.config/pg_autoctl tree, we can't re-use
	 * build_xdg_path() here.
	 *
	 * That said, we don't need the flexibility of build_xdg_path with respect
	 * to XDGResourceType as we know we are dealing with XDG_CONFIG only.
	 */
	if (IS_EMPTY_STRING_BUFFER(pathnames->systemd))
	{
		char filename[MAXPGPATH];
		char *home = getenv("HOME");
		char *xdg_topdir = getenv("XDG_CONFIG_HOME");;

		if (home == NULL)
		{
			log_fatal("Environment variable HOME is unset");
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		if (xdg_topdir != NULL)
		{
			strlcpy(filename, xdg_topdir, MAXPGPATH);
		}
		else
		{
			strlcpy(filename, home, MAXPGPATH);
			join_path_components(filename, filename, ".config");
		}

		join_path_components(filename, filename, "systemd/user");

		/* mkdir -p the target directory */
		if (pg_mkdir_p(filename, 0755) == -1)
		{
			log_error("Failed to create state directory \"%s\": %s",
					  filename, strerror(errno));
			return false;
		}

		/* and finally add the configuration file name */
		join_path_components(filename, filename, KEEPER_SYSTEMD_FILENAME);

		if (!normalize_filename(filename, pathnames->systemd, MAXPGPATH))
		{
			/* errors have already been logged */
			return false;
		}
	}

	log_trace("SetSystemdFilePath: \"%s\"", pathnames->systemd);

	return true;
}
