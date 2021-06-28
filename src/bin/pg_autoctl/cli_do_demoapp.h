/*
 * src/bin/pg_autoctl/cli_do_demoapp.h
 *     Implementation of a demo application that shows how to handle automatic
 *     reconnection when a failover happened, and uses a single URI.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef CLI_DO_DEMOAPP_H
#define CLI_DO_DEMOAPP_H

#include "postgres_fe.h"
#include "pqexpbuffer.h"
#include "snprintf.h"

#include "cli_common.h"
#include "cli_do_root.h"
#include "cli_root.h"
#include "commandline.h"
#include "config.h"
#include "env_utils.h"
#include "log.h"
#include "pidfile.h"
#include "signals.h"
#include "string_utils.h"

#define MAX_CLIENTS_COUNT 128

typedef struct DemoAppOptions
{
	char monitor_pguri[MAXCONNINFO];
	char formation[NAMEDATALEN];
	char username[NAMEDATALEN];
	int groupId;

	int clientsCount;
	int duration;
	int firstFailover;
	int failoverFreq;
	bool doFailover;
} DemoAppOptions;

extern DemoAppOptions demoAppOptions;

#endif  /* CLI_DO_DEMOAPP_H */
