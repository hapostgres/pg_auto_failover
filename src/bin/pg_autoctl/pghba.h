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


/* supported HBA connection values */
typedef enum HBAConnectionType
{
	HBA_CONNECTION_LOCAL,
	HBA_CONNECTION_HOST,
	HBA_CONNECTION_HOSTSSL,
	HBA_CONNECTION_HOSTNOSSL,
	HBA_CONNECTION_HOSTGSSENC,
	HBA_CONNECTION_HOSTNOGSSENC
} HBAConnectionType;

bool pghba_ensure_host_rule_exists(const char *hbaFilePath,
								   HBAConnectionType connectionType,
								   HBADatabaseType,
								   const char *database,
								   const char *username,
								   const char *hostname,
								   const char *authenticationScheme);
bool override_pg_hba_with_only_domain_socket_access(const char *hbaFilePath);

bool pghba_enable_lan_cidr(PGSQL *pgsql,
						   bool ssl,
						   HBADatabaseType databaseType,
						   const char *database,
						   const char *hostname,
						   const char *username,
						   const char *authenticationScheme,
						   const char *pgdata);


#endif /* PGHBA_H */
