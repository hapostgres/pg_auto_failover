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

bool demoapp_grab_formation_uri(DemoAppOptions *options,
								char *pguri, size_t size);

#endif /* DEMOAPP_H */
