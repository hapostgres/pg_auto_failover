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

#include "postgres_fe.h"
#include "pqexpbuffer.h"

#include "keeper.h"
#include "keeper_config.h"
#include "monitor.h"
#include "monitor_config.h"

/*
 * As of pg_autoctl 1.4, the contents of the pidfile is:
 *
 * line #
 *		1	supervisor PID
 *		2	data directory path
 *		3	version number (PG_AUTOCTL_VERSION)
 *		4	shared semaphore id (used to serialize log writes)
 *		5	first supervised service pid line
 *		6	second supervised service pid line
 *    ...
 *
 * The supervised service lines are added later, not the first tim we create
 * the pidfile. Each service line contains 3 bits of information, separated
 * with spaces:
 *
 *   pid service-name
 *
 * Each service creates its own pidfile with its own version number. At
 * pg_autoctl upgrade time, we might have a supervisor process that's running
 * with a different version than one of the restarted pg_autoctl services.
 */
#define PIDFILE_LINE_PID 1
#define PIDFILE_LINE_DATA_DIR 2
#define PIDFILE_LINE_VERSION_STRING 3
#define PIDFILE_LINE_SEM_ID 4
#define PIDFILE_LINE_FIRST_SERVICE 5

bool create_pidfile(const char *pidfile, pid_t pid);

bool prepare_pidfile_buffer(PQExpBuffer content, pid_t pid);
bool create_service_pidfile(const char *pidfile, const char *serviceName);
void get_service_pidfile(const char *pidfile,
						 const char *serviceName,
						 char *filename);
bool read_service_pidfile_version_string(const char *pidfile,
										 char *versionString);

bool read_pidfile(const char *pidfile, pid_t *pid);
bool remove_pidfile(const char *pidfile);
void check_pidfile(const char *pidfile, pid_t start_pid);


#endif /* PIDFILE_H */
