/*-------------------------------------------------------------------------
 *
 * src/monitor/conninfo.c
 *
 * This file contains functions to get the primary connection info from
 * recovery.conf.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "c.h"
#include "fmgr.h"
#include "funcapi.h"
#include "libpq-fe.h"
#include "miscadmin.h"
#include "storage/fd.h"
#include "utils/guc.h"

#include "conninfo.h"

/* named constants */
#define RECOVERY_COMMAND_FILE "recovery.conf"


/* private function declarations */
static char * ReadPrimaryConnInfoFromRecoveryConf(void);


/*
 * ReadPrimaryHostAddress reads the hostname and port as defined in recovery.conf
 * into the parameters.
 */
int
ReadPrimaryHostAddress(char **primaryName, char **primaryPort)
{
	char *errorMessage = NULL;
	PQconninfoOption *currentOption = NULL;

	char *connInfo = ReadPrimaryConnInfoFromRecoveryConf();
	if (connInfo == NULL)
	{
		return -1;
	}

	PQconninfoOption *options = PQconninfoParse(connInfo, &errorMessage);
	if (options == NULL)
	{
		pfree(connInfo);
		return -1;
	}

	for (currentOption = options; currentOption->keyword != NULL; currentOption++)
	{
		char *keyword = currentOption->keyword;
		char *value = currentOption->val;

		if (value == NULL)
		{
			continue;
		}

		if (strcmp(keyword, "host") == 0 ||
			strcmp(keyword, "hostaddr") == 0)
		{
			*primaryName = pstrdup(value);
		}
		else if (strcmp(keyword, "port") == 0)
		{
			*primaryPort = pstrdup(value);
		}
	}

	PQconninfoFree(options);
	pfree(connInfo);

	return 0;
}


/*
 * ReadPrimaryConnInfoFromRecoveryConf gets the unaltered primary_conninfo
 * field from the recovery.conf file.
 */
static char *
ReadPrimaryConnInfoFromRecoveryConf(void)
{
	ConfigVariable *item = NULL;
	ConfigVariable *head = NULL;
	ConfigVariable *tail = NULL;
	char *primaryConnInfo = NULL;

	FILE *fd = AllocateFile(RECOVERY_COMMAND_FILE, "r");
	if (fd == NULL)
	{
		ereport(LOG, (errcode_for_file_access(),
					  errmsg("could not open recovery command file \"%s\": %m",
							 RECOVERY_COMMAND_FILE)));
		return NULL;
	}

	/*
	 * Since we're asking ParseConfigFp() to report errors as FATAL, there's
	 * no need to check the return value.
	 */
	(void) ParseConfigFp(fd, RECOVERY_COMMAND_FILE, 0, FATAL, &head, &tail);

	FreeFile(fd);

	for (item = head; item; item = item->next)
	{
		if (strcmp(item->name, "primary_conninfo") == 0)
		{
			primaryConnInfo = pstrdup(item->value);
		}
	}

	FreeConfigVariables(head);

	return primaryConnInfo;
}
