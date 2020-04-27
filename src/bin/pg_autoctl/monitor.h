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

#include <stdbool.h>

#include "pgsql.h"
#include "monitor_config.h"
#include "state.h"


/* interface to the monitor */
typedef struct Monitor
{
	PGSQL pgsql;
	MonitorConfig config;
} Monitor;

typedef struct MonitorAssignedState
{
	int nodeId;
	int groupId;
	NodeState state;
	int candidatePriority;
	bool replicationQuorum;
} MonitorAssignedState;

typedef struct StateNotification
{
	char message[BUFSIZE];
	NodeState reportedState;
	NodeState goalState;
	char formationId[NAMEDATALEN];
	int groupId;
	int nodeId;
	char nodeName[_POSIX_HOST_NAME_MAX];
	char hostName[_POSIX_HOST_NAME_MAX];
	int nodePort;
} StateNotification;

typedef struct MonitorExtensionVersion
{
	char defaultVersion[BUFSIZE];
	char installedVersion[BUFSIZE];
} MonitorExtensionVersion;

bool monitor_init(Monitor *monitor, char *url);
void monitor_finish(Monitor *monitor);

void printNodeHeader(int maxNodeNameSize, int maxHostNameSize);
void printNodeEntry(NodeAddress *node, int maxNodeNameSize, int maxHostNameSize);
void prepareHeaderSeparator(char *buffer, int size);

bool monitor_get_nodes(Monitor *monitor, char *formation, int groupId,
					   NodeAddressArray *nodeArray);
bool monitor_print_nodes(Monitor *monitor, char *formation, int groupId);
bool monitor_print_nodes_as_json(Monitor *monitor, char *formation, int groupId);
bool monitor_get_other_nodes(Monitor *monitor,
							 char *myHost, int myPort, NodeState currentState,
							 NodeAddressArray *nodeArray);
bool monitor_print_other_nodes(Monitor *monitor,
							   char *myHost, int myPort, NodeState currentState);
bool monitor_print_other_nodes_as_json(Monitor *monitor,
									   char *myHost, int myPort,
									   NodeState currentState);
void printNodeArray(NodeAddressArray *nodesArray);

bool monitor_get_primary(Monitor *monitor, char *formation, int groupId,
						 NodeAddress *node);
bool monitor_get_coordinator(Monitor *monitor, char *formation,
							 NodeAddress *node);
bool monitor_register_node(Monitor *monitor,
						   char *formation,
						   char *name,
						   char *host,
						   int port,
						   char *dbname,
						   int desiredGroupId,
						   NodeState initialState,
						   PgInstanceKind kind,
						   int candidatePriority,
						   bool quorum,
						   MonitorAssignedState *assignedState);
bool monitor_node_active(Monitor *monitor,
						 char *formation, char *host, int port, int nodeId,
						 int groupId, NodeState currentState,
						 bool pgIsRunning,
						 char *currentLSN, char *pgsrSyncState,
						 MonitorAssignedState *assignedState);
bool monitor_get_node_replication_settings(Monitor *monitor, int nodeid,
										   NodeReplicationSettings *settings);
bool monitor_set_node_candidate_priority(Monitor *monitor, int nodeid,
										 char *nodeName, int nodePort,
										 int candidatePriority);
bool monitor_set_node_replication_quorum(Monitor *monitor, int nodeid,
										 char *nodeName, int nodePort,
										 bool replicationQuorum);
bool monitor_get_formation_number_sync_standbys(Monitor *monitor, char *formation,
												int *numberSyncStandbys);
bool monitor_set_formation_number_sync_standbys(Monitor *monitor, char *formation,
												int numberSyncStandbys);

bool monitor_remove(Monitor *monitor, char *host, int port);
bool monitor_perform_failover(Monitor *monitor, char *formation, int group);

bool monitor_print_state(Monitor *monitor, char *formation, int group);
bool monitor_print_last_events(Monitor *monitor,
							   char *formation, int group, int count);
bool monitor_print_state_as_json(Monitor *monitor, char *formation, int group);
bool monitor_print_last_events_as_json(Monitor *monitor,
									   char *formation, int group,
									   int count,
									   FILE *stream);

bool monitor_print_every_formation_uri(Monitor *monitor, const SSLOptions *ssl);
bool monitor_print_every_formation_uri_as_json(Monitor *monitor,
											   const SSLOptions *ssl,
											   FILE *stream);

bool monitor_create_formation(Monitor *monitor, char *formation, char *kind,
							  char *dbname, bool ha, int numberSyncStandbys);
bool monitor_enable_secondary_for_formation(Monitor *monitor,
											const char *formation);
bool monitor_disable_secondary_for_formation(Monitor *monitor,
											 const char *formation);

bool monitor_drop_formation(Monitor *monitor, char *formation);

bool monitor_formation_uri(Monitor *monitor,
						   const char *formation,
						   const SSLOptions *ssl,
						   char *connectionString,
						   size_t size);

bool monitor_synchronous_standby_names(Monitor *monitor,
									   char *formation, int groupId,
									   char *synchronous_standby_names,
									   int size);

bool monitor_set_nodename(Monitor *monitor, int nodeId, const char *nodename);

bool monitor_start_maintenance(Monitor *monitor, char *formation, char *nodename);
bool monitor_stop_maintenance(Monitor *monitor, char *formation, char *nodename);

bool monitor_get_notifications(Monitor *monitor);
bool monitor_wait_until_primary_applied_settings(Monitor *monitor,
												 const char *formation);
bool monitor_wait_until_node_reported_state(Monitor *monitor,
											int nodeId,
											NodeState state);

bool monitor_get_extension_version(Monitor *monitor,
								   MonitorExtensionVersion *version);
bool monitor_extension_update(Monitor *monitor, const char *targetVersion);
bool monitor_ensure_extension_version(Monitor *monitor,
									  MonitorExtensionVersion *version);


#endif /* MONITOR_H */
