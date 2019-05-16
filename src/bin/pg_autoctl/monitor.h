/*
 * src/bin/pg_autoctl/monitor.h
 *     Functions for interacting with a pg_auto_failover monitor
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef MONITOR_H
#define MONITOR_H


#include "pgsql.h"
#include "state.h"


/* interface to the monitor */
typedef struct Monitor
{
	PGSQL pgsql;
} Monitor;

typedef struct MonitorAssignedState
{
	int nodeId;
	int groupId;
	NodeState state;
} MonitorAssignedState;

typedef struct StateNotification
{
	char        message[BUFSIZE];
	NodeState	reportedState;
	NodeState	goalState;
	char	    formationId[NAMEDATALEN];
	int			groupId;
	int			nodeId;
	char	    nodeName[_POSIX_HOST_NAME_MAX];
	int			nodePort;
} StateNotification;

typedef struct MonitorExtensionVersion
{
	char defaultVersion[BUFSIZE];
	char installedVersion[BUFSIZE];
} MonitorExtensionVersion;

bool monitor_init(Monitor *monitor, char *url);
void monitor_finish(Monitor *monitor);
bool monitor_get_other_node(Monitor *monitor, char *myHost, int myPort,
							NodeAddress *otherNode);
bool monitor_get_primary(Monitor *monitor, char *formation, int groupId,
						 NodeAddress *node);
bool monitor_get_coordinator(Monitor *monitor, char *formation,
							 NodeAddress *node);
bool monitor_register_node(Monitor *monitor, char *formation, char *host, int port,
						   char *dbname, int desiredGroupId, NodeState initialSate,
						   PgInstanceKind kind,
						   MonitorAssignedState *assignedState);
bool monitor_node_active(Monitor *monitor,
						 char *formation, char *host, int port, int nodeId,
						 int groupId, NodeState currentState,
						 bool pgIsRunning,
						 uint64_t replicationLag, char *pgsrSyncState,
						 MonitorAssignedState *assignedState);
bool monitor_remove(Monitor *monitor, char *host, int port);
bool monitor_print_state(Monitor *monitor, char *formation, int group);
bool monitor_print_last_events(Monitor *monitor,
							   char *formation, int group, int count);

bool monitor_create_formation(Monitor *monitor, char *formation, char *kind,
							  char *dbname, bool ha);
bool monitor_enable_secondary_for_formation(Monitor *monitor, const char *formation);
bool monitor_disable_secondary_for_formation(Monitor *monitor, const char *formation);
bool monitor_drop_formation(Monitor *monitor, char *formation);
bool monitor_formation_uri(Monitor *monitor, const char *formation,
						   char *connectionString, size_t size);

bool monitor_start_maintenance(Monitor *monitor, char *host, int port);
bool monitor_stop_maintenance(Monitor *monitor, char *host, int port);

bool monitor_get_notifications(Monitor *monitor);

bool monitor_get_extension_version(Monitor *monitor,
								   MonitorExtensionVersion *version);
bool monitor_extension_update(Monitor *monitor, char *targetVersion);
bool monitor_ensure_extension_version(Monitor *monitor);

#endif /* MONITOR_H */
