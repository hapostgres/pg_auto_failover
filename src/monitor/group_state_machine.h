/*-------------------------------------------------------------------------
 *
 * src/monitor/group_state_machine.h
 *
 * Declarations for public functions and types related to a group state
 * machine.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */
#pragma once

#include "postgres.h"

#include "access/xlogdefs.h"
#include "datatype/timestamp.h"
#include "formation_metadata.h"
#include "node_metadata.h"

/*
 * MonitorFSMSection identifies which node of the section hierarchy a row's
 * .sectionPath (see MonitorFSMTransition/MonitorFSMSectionPath in
 * group_state_machine.c) belongs to at some depth -- a row's own path is a
 * small array of these, e.g. { MONITOR_FSM_SECTION_REPORTING_NODE,
 * MONITOR_FSM_SECTION_MS_FAILOVER, MONITOR_FSM_SECTION_MS_FAILOVER_RETRY_RESET },
 * matched via prefix/ancestor containment (SectionPathIsUnderPrefix) instead
 * of the hand-maintained array-index-range constants this replaces -- see
 * that mechanism's own comment for why (the design doc's own "Open items"
 * flagged the old index constants as needing "to stay in sync with the
 * table by hand as rows are added, removed, or reordered").
 *
 * Only the first four values below (API_TRIGGERED/EARLY_CHECKS/
 * REPORTING_NODE/PRIMARY_NODE, unchanged in name and meaning from before this
 * mechanism existed) are ever legal as a row's .sectionPath[0] -- every row's
 * path always starts with exactly one of these four, enforced by
 * AssertMonitorFSMWellFormed(). Declared here (not just in the .c file) so
 * this top-level tag can be exposed to SQL as pgautofailover.fsm_section
 * (mapped by NAME, not ordinal -- see MonitorFSMSectionGetEnum/
 * EnumGetMonitorFSMSection -- so inserting MONITOR_FSM_SECTION_NONE ahead of
 * them, or appending new fine-grained values after PRIMARY_NODE, changes no
 * SQL-visible behavior at all), the same way ReplicationState is exposed as
 * pgautofailover.replication_state (see replication_state.h/.c for that
 * pattern, mirrored below).
 *
 * API_TRIGGERED comes first (pos 101-1xx) even though it's the newest
 * section, chronologically: an operator-triggered call (perform_failover,
 * remove_node, start/stop_maintenance, set_node_candidate_priority,
 * set_node_replication_quorum, set_formation_number_sync_standbys) is a
 * distinct, self-contained entry point, not a continuation of the
 * heartbeat-driven EARLY_CHECKS/REPORTING_NODE/PRIMARY_NODE chain -- placing
 * it in its own leading hundred-block keeps that same "each section is one
 * contiguous pos range, matching one real entry point" property the other
 * three sections already have, rather than wedging operator rows into gaps
 * within the heartbeat sections.
 *
 * Values from MONITOR_FSM_SECTION_FROM_CONTEXT onward are fine-grained leaf
 * labels, only ever used at path depth 1+ (reporting_node.from_context,
 * reporting_node.ms_failover, and its own sub-leaves) -- purely for
 * dump_fsm()'s own section_path column readability today; no caller needs
 * to bound a search this narrowly (see each leaf's own row comment in
 * MonitorFSM[]).
 */
typedef enum MonitorFSMSection
{
	MONITOR_FSM_SECTION_NONE = 0,      /* terminator / unused trailing path slot */
	MONITOR_FSM_SECTION_API_TRIGGERED,
	MONITOR_FSM_SECTION_EARLY_CHECKS,
	MONITOR_FSM_SECTION_REPORTING_NODE,
	MONITOR_FSM_SECTION_PRIMARY_NODE,

	MONITOR_FSM_SECTION_FROM_CONTEXT,  /* reporting_node.from_context */
	MONITOR_FSM_SECTION_MS_FAILOVER,   /* reporting_node.ms_failover */
	MONITOR_FSM_SECTION_MS_FAILOVER_RETRY_RESET,
	MONITOR_FSM_SECTION_MS_FAILOVER_CANDIDATE_JOIN,
	MONITOR_FSM_SECTION_MS_FAILOVER_CANDIDATE_FANOUT,
	MONITOR_FSM_SECTION_MS_FAILOVER_PROMOTION_OUTCOME,
	MONITOR_FSM_SECTION_MS_FAILOVER_PROMOTION_OUTCOME_MISSING_NODES_GATE,
	MONITOR_FSM_SECTION_MS_FAILOVER_PROMOTION_OUTCOME_CANDIDATE_COUNT_GATE,
	MONITOR_FSM_SECTION_MS_FAILOVER_PROMOTION_OUTCOME_QUORUM_CANDIDATE_GATE,
	MONITOR_FSM_SECTION_MS_FAILOVER_PROMOTION_OUTCOME_NO_CANDIDATE_YET,
	MONITOR_FSM_SECTION_MS_FAILOVER_DRAINING_OR_MAINTENANCE,
} MonitorFSMSection;

/* public function declarations, mirroring replication_state.h's pattern */
extern Oid MonitorFSMSectionTypeOid(void);
extern MonitorFSMSection EnumGetMonitorFSMSection(Oid monitorFSMSectionOid);
extern Oid MonitorFSMSectionGetEnum(MonitorFSMSection section);
extern const char * MonitorFSMSectionGetName(MonitorFSMSection section);

/*
 * MonitorApiFunction identifies which operator-triggered SQL entry point
 * produced a ProceedGroupStateForApiTrigger() dispatch call -- see that
 * function's own comment in group_state_machine.c, and the
 * MONITOR_FSM_SECTION_API_TRIGGERED rows in MonitorFSM[] that match against
 * it via .conditions.apiTrigger. API_FUNCTION_NONE is never passed to
 * ProceedGroupStateForApiTrigger() itself -- it's the ordinary node_active()
 * heartbeat path's own implicit value (see NodeActiveContext's apiFunction
 * field), kept ordinal 0 so every row written before this mechanism existed
 * keeps meaning exactly what it always meant, with zero changes.
 */
typedef enum MonitorApiFunction
{
	API_FUNCTION_NONE = 0,
	API_FUNCTION_REMOVE_NODE,
	API_FUNCTION_PERFORM_FAILOVER,
	API_FUNCTION_START_MAINTENANCE,
	API_FUNCTION_STOP_MAINTENANCE,
	API_FUNCTION_SET_NODE_CANDIDATE_PRIORITY,
	API_FUNCTION_SET_NODE_REPLICATION_QUORUM,
	API_FUNCTION_SET_FORMATION_NUMBER_SYNC_STANDBYS,
} MonitorApiFunction;

/*
 * AutoFailoverNodeState describes the current state of a node in a group.
 */
typedef struct AutoFailoverNodeState
{
	int64 nodeId;
	int32 groupId;
	ReplicationState replicationState;
	int32 reportedTLI;
	XLogRecPtr reportedLSN;
	SyncState pgsrSyncState;
	bool pgIsRunning;
	int candidatePriority;
	bool replicationQuorum;
} AutoFailoverNodeState;


/*
 * GroupStateContext bundles every input the monitor node_active protocol needs
 * to make FSM decisions: the node list (loaded once from the DB), the
 * formation, a single timestamp snapshot, and copies of the relevant GUCs.
 *
 * Production code builds this with BuildGroupStateContext(), which fetches
 * everything from the database.  Test code can populate it from fixtures and
 * call ProceedGroupStateFromContext() directly, making the FSM logic
 * exercisable without a live database.
 */
typedef struct GroupStateContext
{
	char *formationId;
	int groupId;
	AutoFailoverNode *activeNode;
	List *groupNodeList;
	int groupNodeCount;
	AutoFailoverFormation *formation;
	TimestampTz now;
	int unhealthyTimeoutMs;
	int drainTimeoutMs;
	int startupGracePeriodMs;
	int replicationStallTimeoutMs;
} GroupStateContext;


/* public function declarations */
extern bool BuildGroupStateContext(GroupStateContext *ctx,
								   AutoFailoverNode *activeNode);
extern bool ProceedGroupState(AutoFailoverNode *activeNode);
extern bool ProceedGroupStateFromContext(GroupStateContext *ctx);
extern bool ProceedGroupStateForApiTrigger(MonitorApiFunction apiFunction,
										   AutoFailoverNode *activeNode,
										   AutoFailoverNode *primaryNode);

/* GUCs */
extern int EnableSyncXlogThreshold;
extern int PromoteXlogThreshold;
extern int DrainTimeoutMs;
extern int UnhealthyTimeoutMs;
extern int StartupGracePeriodMs;
extern int ReplicationStallTimeoutMs;
