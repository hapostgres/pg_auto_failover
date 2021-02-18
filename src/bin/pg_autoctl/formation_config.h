/*
 * src/bin/pg_autoctl/formation_config.h
 *     Formation configuration data structure and function definitions for cli
 *     commands targeting formations.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef FORMATION_CONFIG_H
#define FORMATION_CONFIG_H

#include "pgctl.h"

typedef struct FormationConfig
{
	/* pg_auto_failover formation setup */
	char monitor_pguri[MAXCONNINFO];

	char formation[NAMEDATALEN];
	char formationKind[NAMEDATALEN];
	char dbname[NAMEDATALEN];
	bool formationHasSecondary;
	int numberSyncStandbys;

	/* PostgreSQL setup */
	PostgresSetup pgSetup;
} FormationConfig;

#endif /*FORMATION_CONFIG_H */
