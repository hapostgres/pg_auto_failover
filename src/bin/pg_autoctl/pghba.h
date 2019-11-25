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


bool pghba_ensure_host_rule_exists(const char *hbaFilePath,
								   HBADatabaseType,
								   const char *database,
								   const char *username,
								   const char *hostname,
								   const char *authenticationScheme);

bool pghba_enable_lan_cidr(const char *hbaFilePath,
						   HBADatabaseType databaseType,
						   const char *database,
						   const char *hostname,
						   const char *username,
						   const char *authenticationScheme,
						   PGSQL *pgsql);

#endif /* PGHBA_H */
