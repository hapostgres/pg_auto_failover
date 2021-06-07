/*
 * src/bin/pg_autoctl/archiver_config.h
 *     Archiver configuration data structure and function definitions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef ARCHIVER_CONFIG_H
#define ARCHIVER_CONFIG_H

#include <limits.h>
#include <stdbool.h>

#include "config.h"
#include "defaults.h"
#include "pgctl.h"
#include "pgsql.h"

typedef struct ArchiverConfig
{
	/* in-memory configuration related variables */
	ConfigFilePaths pathnames;

	char directory[MAXPGPATH];

	char role[NAMEDATALEN];
	char monitor_pguri[MAXCONNINFO];
	char name[_POSIX_HOST_NAME_MAX];
	char hostname[_POSIX_HOST_NAME_MAX];
} ArchiverConfig;


bool archiver_config_set_pathnames_from_directory(ArchiverConfig *config);
void archiver_config_init(ArchiverConfig *config);
bool archiver_config_read_file(ArchiverConfig *config);
bool archiver_config_write_file(ArchiverConfig *config);
bool archiver_config_write(FILE *stream, ArchiverConfig *config);
bool archiver_config_merge_options(ArchiverConfig *config,
								   ArchiverConfig *options);

bool archiver_config_to_json(ArchiverConfig *config, JSON_Value *js);
void archiver_config_log_settings(ArchiverConfig *config);

bool archiver_config_get_setting(ArchiverConfig *config,
								 const char *path,
								 char *value, size_t size);

bool archiver_config_set_setting(ArchiverConfig *config,
								 const char *path,
								 char *value);

bool archiver_config_update_with_absolute_pgdata(ArchiverConfig *config);

bool archiver_config_print_from_file(const char *pathname,
									 bool outputContents,
									 bool outputJSON);


#endif /* ARCHIVER_CONFIG_H */
