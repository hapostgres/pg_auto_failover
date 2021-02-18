/*
 * src/bin/pg_autoctl/archiver.c
 *	 API for interacting with the archiver
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <inttypes.h>
#include <limits.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "archiver.h"
#include "archiver_config.h"
#include "defaults.h"
#include "env_utils.h"
#include "log.h"
#include "parsing.h"
#include "string_utils.h"


/*
 * archiver_monitor_int initialises a connection to the monitor.
 */
bool
archiver_monitor_init(Archiver *archiver)
{
	if (!monitor_init(&(archiver->monitor), archiver->config.monitor_pguri))
	{
		/* errors have already been logged */
		return false;
	}

	return true;
}
