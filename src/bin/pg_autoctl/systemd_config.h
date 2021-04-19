/*
 * src/bin/pg_autoctl/systemd_config.h
 *     Keeper integration with systemd service configuration file
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SYSTEMD_CONFIG_H
#define SYSTEMD_CONFIG_H

#include <limits.h>
#include <stdbool.h>

#include "config.h"

typedef struct SystemdServiceConfig
{
	ConfigFilePaths pathnames;

	/* UNIT */
	char Description[BUFSIZE];

	/* Service */
	char WorkingDirectory[MAXPGPATH];
	char EnvironmentPGDATA[BUFSIZE];
	char User[NAMEDATALEN];
	char ExecStart[BUFSIZE];
	char Restart[BUFSIZE];
	int StartLimitBurst;
	char ExecReload[BUFSIZE];

	/* Install */
	char WantedBy[BUFSIZE];

	/* PostgreSQL setup */
	PostgresSetup pgSetup;
} SystemdServiceConfig;

void systemd_config_init(SystemdServiceConfig *config, const char *pgdata);
bool systemd_config_write_file(SystemdServiceConfig *config);
bool systemd_config_write(FILE *stream, SystemdServiceConfig *config);

#endif /* SYSTEMD_CONFIG_H */
