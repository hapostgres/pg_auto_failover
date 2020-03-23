/*
 * src/bin/pg_autoctl/service.h
 *   Utilities to start/stop the pg_autoctl service.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#ifndef SERVICE_H
#define SERVICE_H

#include <inttypes.h>
#include <signal.h>

#include "keeper.h"
#include "keeper_config.h"
#include "monitor.h"
#include "monitor_config.h"

bool monitor_service_init(MonitorConfig *config, pid_t *pid);
bool keeper_service_init(Keeper *keeper, pid_t *pid);

bool service_stop(ConfigFilePaths *pathnames);

bool create_pidfile(const char *pidfile, pid_t pid);
bool read_pidfile(const char *pidfile, pid_t *pid);
bool remove_pidfile(const char *pidfile);
void check_pidfile(const char *pidfile, pid_t start_pid);


#endif /* SERVICE_H */
