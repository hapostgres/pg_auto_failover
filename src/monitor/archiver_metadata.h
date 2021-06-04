/*-------------------------------------------------------------------------
 *
 * src/monitor/archiver_metadata.h
 *
 * Declarations for public functions and types related to archiver metadata.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "access/xlogdefs.h"
#include "datatype/timestamp.h"

#define AUTO_FAILOVER_ARCHIVER_TABLE_NAME "archiver"

/* column indexes for pgautofailover.archiver */
#define Natts_pgautofailover_archiver 3
#define Anum_pgautofailover_archiver_archiverid 1
#define Anum_pgautofailover_archiver_nodename 2
#define Anum_pgautofailover_archiver_nodehost 3

/*
 * AutoFailoverArchiver
 */
typedef struct AutoFailoverArchiver
{
	int archiverId;
	char *nodeName;
	char *nodeHost;
} AutoFailoverArchiver;


AutoFailoverArchiver * GetArchiver(int archiverId);
AutoFailoverArchiver * GetArchiverByHost(const char *nodeHost);
int AddArchiver(const char *nodeHost, const char *nodeName);
void RemoveArchiver(AutoFailoverArchiver *archiver);

bool AddArchiverNode(AutoFailoverArchiver *archiver, int nodeId, int groupId);

bool AddArchiverPolicyForMonitor(AutoFailoverArchiver *archiver);
