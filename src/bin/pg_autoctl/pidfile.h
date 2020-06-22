/*
 * src/bin/pg_autoctl/pidfile.h
 *   Utilities to manage the pg_autoctl pidfile.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#ifndef PIDFILE_H
#define PIDFILE_H

#include <inttypes.h>
#include <signal.h>

#include "keeper.h"
#include "keeper_config.h"
#include "monitor.h"
#include "monitor_config.h"

bool create_pidfile(const char *pidfile, pid_t pid);
bool read_pidfile(const char *pidfile, pid_t *pid);
bool remove_pidfile(const char *pidfile);
void check_pidfile(const char *pidfile, pid_t start_pid);


#endif /* PIDFILE_H */
