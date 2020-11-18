/*
 * src/bin/pg_autoctl/config.h
 *     Common configuration data structure and function definitions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <limits.h>
#include <stdbool.h>

#include "pgctl.h"
#include "pgsql.h"

#define KEEPER_ROLE "keeper"
#define MONITOR_ROLE "monitor"

typedef enum
{
	PG_AUTOCTL_ROLE_UNKNOWN,
	PG_AUTOCTL_ROLE_MONITOR,
	PG_AUTOCTL_ROLE_KEEPER
} pgAutoCtlNodeRole;

typedef struct MinimalConfig
{
	char role[NAMEDATALEN];
} MinimalConfig;

typedef struct ConfigFilePaths
{
	char config[MAXPGPATH]; /* ~/.config/pg_autoctl/${PGDATA}/pg_autoctl.cfg */
	char state[MAXPGPATH];  /* ~/.local/share/pg_autoctl/${PGDATA}/pg_autoctl.state */
	char pid[MAXPGPATH];    /* /tmp/${PGDATA}/pg_autoctl.pid */
	char init[MAXPGPATH];   /* /tmp/${PGDATA}/pg_autoctl.init */
	char nodes[MAXPGPATH];  /* ~/.local/share/pg_autoctl/${PGDATA}/nodes.json */
	char systemd[MAXPGPATH];    /* ~/.config/systemd/user/pgautofailover.service */
} ConfigFilePaths;

/*
 * We implement XDG Base Directory Specification (in parts), and the following
 * XDGResourceType makes it possible to make some decisions in the generic
 * build_xdg_path() helper function:
 *
 * - XDG_DATA resource uses XDG_DATA_HOME environment variable and defaults to
 *   ${HOME}.local/share
 *
 * - XDG_CONFIG resource uses XDG_CONFIG_HOME environement variable and
 *   defaults to ${HOME}/.config
 *
 * - XDG_CACHE and XDG_RUNTIME are not implemented yet.
 *
 * https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
 */
typedef enum
{
	XDG_DATA,
	XDG_CONFIG,
	XDG_CACHE,
	XDG_RUNTIME
} XDGResourceType;


bool build_xdg_path(char *dst, XDGResourceType xdgType,
					const char *pgdata, const char *name);

bool SetConfigFilePath(ConfigFilePaths *pathnames, const char *pgdata);
bool SetStateFilePath(ConfigFilePaths *pathnames, const char *pgdata);
bool SetNodesFilePath(ConfigFilePaths *pathnames, const char *pgdata);
bool SetPidFilePath(ConfigFilePaths *pathnames, const char *pgdata);

pgAutoCtlNodeRole ProbeConfigurationFileRole(const char *filename);


#define strneq(x, y) \
	((x != NULL) && (y != NULL) && (strcmp(x, y) != 0))

bool config_accept_new_ssloptions(PostgresSetup *pgSetup,
								  PostgresSetup *newPgSetup);

#endif /* CONFIG_H */
