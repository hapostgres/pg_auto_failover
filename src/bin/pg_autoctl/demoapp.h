/*
 * src/bin/pg_autoctl/demoapp.h
 *	 Demo application for pg_auto_failover
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef DEMOAPP_H
#define DEMOAPP_H

#include <stdbool.h>

#include "cli_do_demoapp.h"

#define DEMO_DEFAULT_RETRY_CAP_TIME 200
#define DEMO_DEFAULT_RETRY_SLEEP_TIME 500

bool demoapp_grab_formation_uri(DemoAppOptions *options,
								char *pguri, size_t size,
								bool *mayRetry);
bool demoapp_prepare_schema(const char *pguri);
bool demoapp_run(const char *pguri, DemoAppOptions *demoAppOptions);

void demoapp_print_histogram(const char *pguri, DemoAppOptions *demoAppOptions);
void demoapp_print_summary(const char *pguri, DemoAppOptions *demoAppOptions);

#endif /* DEMOAPP_H */
