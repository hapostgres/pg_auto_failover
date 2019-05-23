/*
 * src/bin/pg_autoctl/pghba.h
 *   API for manipulating pg_hba.conf files
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef PGHBA_H
#define PGHBA_H

#include "pgsql.h"

/* supported HBA database values */
typedef enum HBADatabaseType
{
	HBA_DATABASE_ALL,
	HBA_DATABASE_REPLICATION,
	HBA_DATABASE_DBNAME
} HBADatabaseType;


bool pghba_ensure_host_rule_exists(char *hbaFilePath, HBADatabaseType, char *database,
								   char *username, char *hostname,
								   char *authenticationScheme);
bool pghba_enable_lan_cidr(PGSQL *pgsql, HBADatabaseType databaseType,
						   char *database,
						   char *hostname,
						   char *username,
						   char *authenticationScheme,
						   char *pgdata);

#endif /* PGHBA_H */
