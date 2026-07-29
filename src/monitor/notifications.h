/*-------------------------------------------------------------------------
 *
 * src/monitor/notifications.h
 *
 * Declarations for public functions and types related to monitor
 * notifications.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"
#include "c.h"

#include "node_metadata.h"
#include "replication_state.h"

/*
 * pg_auto_failover notifies on different channels about every event it
 * produces:
 *
 * - the "state" channel is used when a node's state is assigned to something
 *   new
 *
 * - the "log" channel is used to duplicate message that are sent to the
 *   PostgreSQL logs, in order for a pg_auto_failover monitor client to subscribe to
 *   the chatter without having to actually have the privileges to tail the
 *   PostgreSQL server logs.
 */
#define CHANNEL_STATE "state"
#define CHANNEL_LOG "log"
#define BUFSIZE 8192


void LogAndNotifyMessage(char *message, size_t size, const char *fmt, ...) __attribute__(
	(format(printf, 3, 4)));

int64 NotifyStateChange(AutoFailoverNode *node, char *description);
int64 InsertEvent(AutoFailoverNode *node, char *description);

/*
 * Set by group_state_machine.c's declarative dispatch just before invoking a
 * matched MonitorFSM[] row's extraAction/goal-state assignment, and restored
 * to whatever it was before immediately after (nested dispatch -- the
 * MS-failover cascade's and join_secondary's own bounded nested searches --
 * saves/restores rather than clobbers, so the outer row's own subsequent
 * assignments are still attributed correctly). Lets InsertEvent() attribute
 * the resulting pgautofailover.event row to the rule that produced it
 * (rule_pos/rule_section columns), without threading an extra parameter
 * through AssignGoalState/SetNodeGoalState/NotifyStateChange and every one
 * of their many call sites outside the declarative dispatch table. 0 means
 * "no rule attributed" (an ordinary AssignGoalState call from outside the
 * table, e.g. an operator-triggered SQL function, or ProceedGroupStateFor
 * MSFailover's own hand-written internals): rule_pos/rule_section are left
 * NULL in that case, since 0 is not a valid .pos value (every real section
 * starts at 100 or above).
 */
extern int CurrentMonitorFSMRulePos;
extern int CurrentMonitorFSMRuleSection;
