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

#include "pgsetup.h"
#include "pgsql.h"

/* supported HBA database values */
typedef enum HBADatabaseType
{
	HBA_DATABASE_ALL,
	HBA_DATABASE_REPLICATION,
	HBA_DATABASE_DBNAME
} HBADatabaseType;


bool pghba_ensure_host_rule_exists(const char *hbaFilePath,
								   bool ssl,
								   HBADatabaseType,
								   const char *database,
								   const char *username,
								   const char *hostname,
								   const char *authenticationScheme,
								   HBAEditLevel hbaLevel);

bool pghba_ensure_host_rules_exist(const char *hbaFilePath,
								   NodeAddressArray *nodesArray,
								   bool ssl,
								   const char *database,
								   const char *username,
								   const char *authenticationScheme,
								   HBAEditLevel hbaLevel);

bool pghba_enable_lan_cidr(PGSQL *pgsql,
						   bool ssl,
						   HBADatabaseType databaseType,
						   const char *database,
						   const char *hostname,
						   const char *username,
						   const char *authenticationScheme,
						   HBAEditLevel hbaLevel,
						   const char *pgdata);

bool pghba_check_hostname(const char *hostname, char *ipaddr, size_t size,
						  bool *useHostname);

#endif /* PGHBA_H */
