/*
 * src/bin/pg_autoctl/service_walserver.h
 *   Utilities to create, configure and run a pg_autoctl "walserver" node:
 *   a node whose only job is to supervise `pg_walserver serve` as a plain
 *   child process. It never registers with a monitor and never
 *   participates in the keeper FSM.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#ifndef SERVICE_WALSERVER_H
#define SERVICE_WALSERVER_H

#include <signal.h>

#include "postgres_fe.h"

#include "config.h"

/*
 * WalServerConfig is the minimal, non-Postgres equivalent of KeeperConfig
 * for a pg_walserver node: just enough to find and supervise a single
 * `pg_walserver serve` child process.
 */
typedef struct WalServerConfig
{
	ConfigFilePaths pathnames;

	char pgdata[MAXPGPATH];  /* pg_walserver's own storage root (its --pgdata) */
	int port;                /* 0 means: don't pass --port, let pg_walserver
	                          * use its own default/persisted value */
	char name[_POSIX_HOST_NAME_MAX]; /* optional --name, informational only */
} WalServerConfig;

bool walserver_config_write(WalServerConfig *config);
bool walserver_config_read(const char *path, WalServerConfig *config);

bool start_walserver(WalServerConfig *config);
bool service_walserver_ctl_start(void *context, pid_t *pid);

#endif /* SERVICE_WALSERVER_H */
