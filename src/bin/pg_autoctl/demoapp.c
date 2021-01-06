/*
 * src/bin/pg_autoctl/demoapp.c
 *	 Demo application for pg_auto_failover
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

#include "cli_do_demoapp.h"
#include "defaults.h"
#include "demoapp.h"
#include "env_utils.h"
#include "log.h"
#include "monitor.h"
#include "pgsql.h"
#include "signals.h"
#include "string_utils.h"


/*
 * demoapp_grab_formation_uri connects to the monitor and grabs the formation
 * URI to use in the demo application.
 */
bool
demoapp_grab_formation_uri(DemoAppOptions *options, char *pguri, size_t size)
{
	Monitor monitor = { 0 };

	SSLOptions ssl = { 0 };
	SSLMode sslMode = SSL_MODE_PREFER;
	char *sslModeStr = pgsetup_sslmode_to_string(sslMode);

	ssl.sslMode = sslMode;
	strlcpy(ssl.sslModeStr, sslModeStr, SSL_MODE_STRLEN);

	if (!monitor_init(&monitor, options->monitor_pguri))
	{
		/* errors have already been logged */
		return false;
	}

	if (!monitor_formation_uri(&monitor, options->formation, &ssl, pguri, size))
	{
		log_fatal("Failed to grab the Postgres URI "
				  "to connect to formation \"%s\", see above for details",
				  options->formation);
		return false;
	}

	return true;
}
