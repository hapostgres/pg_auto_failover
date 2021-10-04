/*
 * src/bin/pg_autoctl/watch.h
 *     Implementation of a CLI to show events, states, and URI from the
 *     pg_auto_failover monitor.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WATCH_COLSPECSH
#define WATCH_COLSPECSH

#include "watch.h"

/* Column Specifications, so that we adapt to the actual/current screen size */
typedef enum
{
	COLUMN_TYPE_NAME = 0,
	COLUMN_TYPE_ID,
	COLUMN_TYPE_REPLICATION_QUORUM,
	COLUMN_TYPE_CANDIDATE_PRIORITY,
	COLUMN_TYPE_HOST_PORT,
	COLUMN_TYPE_TLI_LSN,
	COLUMN_TYPE_CONN_HEALTH,
	COLUMN_TYPE_CONN_HEALTH_LAG,
	COLUMN_TYPE_CONN_REPORT_LAG,
	COLUMN_TYPE_REPORTED_STATE,
	COLUMN_TYPE_ASSIGNED_STATE,

	COLUMN_TYPE_LAST
} ColumnType;

typedef struct ColSpec
{
	ColumnType type;
	char name[NAMEDATALEN];
	int len;
} ColSpec;

#define MAX_COL_SPECS 12

typedef struct ColPolicy
{
	char name[NAMEDATALEN];
	int totalSize;
	ColSpec specs[MAX_COL_SPECS];
} ColPolicy;


/*
 * A column policy is a list of column specifications.
 *
 * We have a static list of policies, and we pick one at run-time depending on
 * the current size of the terminal window and depending on the actual data
 * size to be displayed, which is also dynamic.
 */
ColPolicy ColumnPolicies[] = {
	{
		"minimal",
		0,
		{
			{ COLUMN_TYPE_ID, "Id", 0 },
			{ COLUMN_TYPE_REPORTED_STATE, "Reported State", 0 },
			{ COLUMN_TYPE_ASSIGNED_STATE, "Assigned State", 0 },
			{ COLUMN_TYPE_LAST, "", 0 }
		}
	},
	{
		"very terse",
		0,
		{
			{ COLUMN_TYPE_NAME, "Name", 0 },
			{ COLUMN_TYPE_CONN_REPORT_LAG, "Report", 0 },
			{ COLUMN_TYPE_REPORTED_STATE, "Reported State", 0 },
			{ COLUMN_TYPE_ASSIGNED_STATE, "Assigned State", 0 },
			{ COLUMN_TYPE_LAST, "", 0 }
		}
	},
	{
		"quite terse",
		0,
		{
			{ COLUMN_TYPE_NAME, "Name", 0 },
			{ COLUMN_TYPE_ID, "Id", 0 },
			{ COLUMN_TYPE_CONN_REPORT_LAG, "Report", 0 },
			{ COLUMN_TYPE_REPORTED_STATE, "Reported State", 0 },
			{ COLUMN_TYPE_ASSIGNED_STATE, "Assigned State", 0 },
			{ COLUMN_TYPE_LAST, "", 0 }
		}
	},
	{
		"terse",
		0,
		{
			{ COLUMN_TYPE_NAME, "Name", 0 },
			{ COLUMN_TYPE_CONN_HEALTH, "Connection", 0 },
			{ COLUMN_TYPE_CONN_REPORT_LAG, "Report", 0 },
			{ COLUMN_TYPE_REPORTED_STATE, "Reported State", 0 },
			{ COLUMN_TYPE_ASSIGNED_STATE, "Assigned State", 0 },
			{ COLUMN_TYPE_LAST, "", 0 }
		}
	},
	{
		"standard",
		0,
		{
			{ COLUMN_TYPE_NAME, "Name", 0 },
			{ COLUMN_TYPE_ID, "Id", 0 },
			{ COLUMN_TYPE_CONN_HEALTH, "Connection", 0 },
			{ COLUMN_TYPE_CONN_REPORT_LAG, "Last Report", 0 },
			{ COLUMN_TYPE_REPORTED_STATE, "Reported State", 0 },
			{ COLUMN_TYPE_ASSIGNED_STATE, "Assigned State", 0 },
			{ COLUMN_TYPE_LAST, "", 0 }
		}
	},
	{
		"semi verbose",
		0,
		{
			{ COLUMN_TYPE_NAME, "Name", 0 },
			{ COLUMN_TYPE_ID, "Id", 0 },
			{ COLUMN_TYPE_CONN_HEALTH_LAG, "Check", 0 },
			{ COLUMN_TYPE_CONN_HEALTH, "Connection", 0 },
			{ COLUMN_TYPE_CONN_REPORT_LAG, "Report", 0 },
			{ COLUMN_TYPE_REPORTED_STATE, "Reported State", 0 },
			{ COLUMN_TYPE_ASSIGNED_STATE, "Assigned State", 0 },
			{ COLUMN_TYPE_LAST, "", 0 }
		}
	},
	{
		"verbose",
		0,
		{
			{ COLUMN_TYPE_NAME, "Name", 0 },
			{ COLUMN_TYPE_ID, "Node", 0 },
			{ COLUMN_TYPE_REPLICATION_QUORUM, "Quorum", 0 },
			{ COLUMN_TYPE_CANDIDATE_PRIORITY, "Priority", 0 },
			{ COLUMN_TYPE_CONN_HEALTH_LAG, "Check", 0 },
			{ COLUMN_TYPE_CONN_HEALTH, "Connection", 0 },
			{ COLUMN_TYPE_CONN_REPORT_LAG, "Report", 0 },
			{ COLUMN_TYPE_REPORTED_STATE, "Reported State", 0 },
			{ COLUMN_TYPE_ASSIGNED_STATE, "Assigned State", 0 },
			{ COLUMN_TYPE_LAST, "", 0 }
		}
	},
	{
		"almost full",
		0,
		{
			{ COLUMN_TYPE_NAME, "Name", 0 },
			{ COLUMN_TYPE_ID, "Node", 0 },
			{ COLUMN_TYPE_REPLICATION_QUORUM, "Quorum", 0 },
			{ COLUMN_TYPE_CANDIDATE_PRIORITY, "Priority", 0 },
			{ COLUMN_TYPE_TLI_LSN, "TLI: LSN", 0 },
			{ COLUMN_TYPE_CONN_HEALTH_LAG, "Check", 0 },
			{ COLUMN_TYPE_CONN_HEALTH, "Connection", 0 },
			{ COLUMN_TYPE_CONN_REPORT_LAG, "Report", 0 },
			{ COLUMN_TYPE_REPORTED_STATE, "Reported State", 0 },
			{ COLUMN_TYPE_ASSIGNED_STATE, "Assigned State", 0 },
			{ COLUMN_TYPE_LAST, "", 0 }
		}
	},
	{
		"full",
		0,
		{
			{ COLUMN_TYPE_NAME, "Name", 0 },
			{ COLUMN_TYPE_ID, "Node", 0 },
			{ COLUMN_TYPE_REPLICATION_QUORUM, "Quorum", 0 },
			{ COLUMN_TYPE_CANDIDATE_PRIORITY, "Priority", 0 },
			{ COLUMN_TYPE_HOST_PORT, "Host:Port", 0 },
			{ COLUMN_TYPE_TLI_LSN, "TLI: LSN", 0 },
			{ COLUMN_TYPE_CONN_HEALTH_LAG, "Check", 0 },
			{ COLUMN_TYPE_CONN_HEALTH, "Connection", 0 },
			{ COLUMN_TYPE_CONN_REPORT_LAG, "Last Report", 0 },
			{ COLUMN_TYPE_REPORTED_STATE, "Reported State", 0 },
			{ COLUMN_TYPE_ASSIGNED_STATE, "Assigned State", 0 },
			{ COLUMN_TYPE_LAST, "", 0 }
		}
	},
	{
		"fully verbose",
		0,
		{
			{ COLUMN_TYPE_NAME, "Name", 0 },
			{ COLUMN_TYPE_ID, "Node", 0 },
			{ COLUMN_TYPE_REPLICATION_QUORUM, "Quorum", 0 },
			{ COLUMN_TYPE_CANDIDATE_PRIORITY, "Priority", 0 },
			{ COLUMN_TYPE_HOST_PORT, "Host:Port", 0 },
			{ COLUMN_TYPE_TLI_LSN, "TLI: LSN", 0 },
			{ COLUMN_TYPE_CONN_HEALTH_LAG, "Last Check", 0 },
			{ COLUMN_TYPE_CONN_HEALTH, "Connection", 0 },
			{ COLUMN_TYPE_CONN_REPORT_LAG, "Last Report", 0 },
			{ COLUMN_TYPE_REPORTED_STATE, "Reported State", 0 },
			{ COLUMN_TYPE_ASSIGNED_STATE, "Assigned State", 0 },
			{ COLUMN_TYPE_LAST, "", 0 }
		}
	}
};

int ColumnPoliciesCount = sizeof(ColumnPolicies) / sizeof(ColumnPolicies[0]);

/*
 * Events column specifications and policies.
 */
typedef enum
{
	EVENT_COLUMN_TYPE_ID = 0,
	EVENT_COLUMN_TYPE_TIME,
	EVENT_COLUMN_TYPE_FORMATION,
	EVENT_COLUMN_TYPE_NODE_ID,
	EVENT_COLUMN_TYPE_GROUP_ID,
	EVENT_COLUMN_TYPE_NODE_NAME,
	EVENT_COLUMN_TYPE_NODE_HOST,
	EVENT_COLUMN_TYPE_NODE_PORT,
	EVENT_COLUMN_TYPE_REPORTED_STATE,
	EVENT_COLUMN_TYPE_ASSIGNED_STATE,
	EVENT_COLUMN_TYPE_REPLICATION_STATE,
	EVENT_COLUMN_TYPE_TIMELINE,
	EVENT_COLUMN_TYPE_LSN,
	EVENT_COLUMN_TYPE_CANDIDATE_PRIORITY,
	EVENT_COLUMN_TYPE_REPLICATION_QUORUM,
	EVENT_COLUMN_TYPE_DESCRIPTION,

	EVENT_COLUMN_TYPE_LAST
} EventColumnType;


typedef struct EventColSpec
{
	EventColumnType type;
	char name[NAMEDATALEN];
	int len;
} EventColSpec;

#define MAX_EVENT_COL_SPECS 16

typedef struct EventColPolicy
{
	char name[NAMEDATALEN];
	int totalSize;
	EventColSpec specs[MAX_COL_SPECS];
} EventColPolicy;

/*
 * A column policy is a list of column specifications.
 *
 * We have a static list of policies, and we pick one at run-time depending on
 * the current size of the terminal window and depending on the actual data
 * size to be displayed, which is also dynamic.
 */
EventColPolicy EventColumnPolicies[] = {
	{
		"minimal",
		0,
		{
			{ EVENT_COLUMN_TYPE_ID, "Id", 0 },
			{ EVENT_COLUMN_TYPE_DESCRIPTION, "Description", 0 },
			{ EVENT_COLUMN_TYPE_LAST, "", 0 }
		}
	},
	{
		"standard",
		0,
		{
			{ EVENT_COLUMN_TYPE_TIME, "Event Time", 0 },
			{ EVENT_COLUMN_TYPE_DESCRIPTION, "Description", 0 },
			{ EVENT_COLUMN_TYPE_LAST, "", 0 }
		}
	},
	{
		"almost verbose",
		0,
		{
			{ EVENT_COLUMN_TYPE_ID, "Id", 0 },
			{ EVENT_COLUMN_TYPE_TIME, "Event Time", 0 },
			{ EVENT_COLUMN_TYPE_DESCRIPTION, "Description", 0 },
			{ EVENT_COLUMN_TYPE_LAST, "", 0 }
		}
	},
	{
		"verbose",
		0,
		{
			{ EVENT_COLUMN_TYPE_ID, "Id", 0 },
			{ EVENT_COLUMN_TYPE_TIME, "Event Time", 0 },
			{ EVENT_COLUMN_TYPE_NODE_NAME, "Name", 0 },
			{ EVENT_COLUMN_TYPE_DESCRIPTION, "Description", 0 },
			{ EVENT_COLUMN_TYPE_LAST, "", 0 }
		}
	},
};

int EventColumnPoliciesCount =
	sizeof(EventColumnPolicies) / sizeof(EventColumnPolicies[0]);

#endif  /* WATCH_COLSPECSH */
