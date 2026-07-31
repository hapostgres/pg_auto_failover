/*
 * src/bin/pg_autoctl/fsm.c
 *   Finite State Machine implementation for pg_autoctl.
 *
 * The state machine transitions are decided by the pg_auto_failover monitor
 * and implemented on the local Postgres node by the pg_autoctl service. This
 * is the client-side implementation. We refer to this service as the "keeper",
 * it is the local agent that executes the pg_auto_failover decisions.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <inttypes.h>
#include <time.h>
#include <unistd.h>

#include "defaults.h"
#include "keeper.h"
#include "parson.h"
#include "pgctl.h"
#include "fsm.h"
#include "log.h"
#include "monitor.h"
#include "primary_standby.h"
#include "state.h"


/*
 * Comments displayed in the logs when state changes.
 */
#define COMMENT_INIT_TO_SINGLE \
	"Start as a single node"

#define COMMENT_PRIMARY_TO_SINGLE \
	"Other node was forcibly removed, now single"

#define COMMENT_DEMOTED_TO_SINGLE \
	"Was demoted after a failure, " \
	"but secondary was forcibly removed"

#define COMMENT_LOST_PRIMARY \
	"Primary was forcibly removed"

#define COMMENT_REPLICATION_TO_SINGLE \
	"Went down to force the primary to time out, " \
	"but then it was removed"

#define COMMENT_SINGLE_TO_WAIT_PRIMARY \
	"A new secondary was added"

#define COMMENT_PRIMARY_TO_WAIT_PRIMARY \
	"Secondary became unhealthy"

#define COMMENT_PRIMARY_TO_JOIN_PRIMARY \
	"A new secondary was added"

#define COMMENT_PRIMARY_TO_DRAINING \
	"A failover occurred, stopping writes "

#define COMMENT_PRIMARY_TO_PREPARE_MAINTENANCE \
	"Promoting the standby to enable maintenance on the " \
	"primary, stopping Postgres "

#define COMMENT_PRIMARY_TO_MAINTENANCE \
	"Setting up Postgres in standby mode for maintenance operations"

#define COMMENT_PRIMARY_TO_MAINTENANCE_PROMOTE_SECONDARY \
	"Promoting the standby to enable maintenance on the primary"

#define COMMENT_PRIMARY_TO_DEMOTED \
	"A failover occurred, no longer primary"

#define COMMENT_DRAINING_TO_DEMOTED \
	"Demoted after a failover, no longer primary"

#define COMMENT_DRAINING_TO_DEMOTE_TIMEOUT \
	"Secondary confirms it’s receiving no more writes"

#define COMMENT_DEMOTE_TIMEOUT_TO_DEMOTED \
	"Demote timeout expired"

#define COMMENT_STOP_REPLICATION_TO_WAIT_PRIMARY \
	"Confirmed promotion with the monitor"

#define COMMENT_WAIT_PRIMARY_TO_PRIMARY \
	"A healthy secondary appeared"

#define COMMENT_JOIN_PRIMARY_TO_PRIMARY \
	"A healthy secondary appeared"

#define COMMENT_DEMOTE_TO_PRIMARY \
	"Detected a network partition, " \
	"but monitor didn't do failover"

#define COMMENT_WAIT_STANDBY_TO_CATCHINGUP \
	"The primary is now ready to accept a standby"

#define COMMENT_DEMOTED_TO_CATCHINGUP \
	"A new primary is available. " \
	"First, try to rewind. If that fails, do a pg_basebackup."

#define COMMENT_SECONDARY_TO_CATCHINGUP \
	"Failed to report back to the monitor, " \
	"not eligible for promotion"

#define COMMENT_CATCHINGUP_TO_SECONDARY \
	"Convinced the monitor that I'm up and running, " \
	"and eligible for promotion again"

#define COMMENT_SECONDARY_TO_PREP_PROMOTION \
	"Stop traffic to primary, " \
	"wait for it to finish draining."

#define COMMENT_PROMOTION_TO_STOP_REPLICATION \
	"Prevent against split-brain situations."

#define COMMENT_INIT_TO_WAIT_STANDBY \
	"Start following a primary"

#define COMMENT_SECONARY_TO_WAIT_STANDBY \
	"Registering to a new monitor"

#define COMMENT_SECONDARY_TO_WAIT_MAINTENANCE \
	"Waiting for the primary to disable sync replication before " \
	"going to maintenance."

#define COMMENT_SECONDARY_TO_MAINTENANCE \
	"Suspending standby for manual maintenance."

#define COMMENT_MAINTENANCE_TO_CATCHINGUP \
	"Restarting standby after manual maintenance is done."

#define COMMENT_BLOCKED_WRITES \
	"Promoting a Citus Worker standby after having blocked writes " \
	"from the coordinator."

#define COMMENT_PRIMARY_TO_APPLY_SETTINGS \
	"Apply new pg_auto_failover settings (synchronous_standby_names)"

#define COMMENT_APPLY_SETTINGS_TO_PRIMARY \
	"Back to primary state after having applied new pg_auto_failover settings"

#define COMMENT_SECONDARY_TO_REPORT_LSN \
	"Reporting the last write-ahead log location received"

#define COMMENT_DRAINING_TO_REPORT_LSN \
	"Reporting the last write-ahead log location after draining"

#define COMMENT_DEMOTED_TO_REPORT_LSN \
	"Reporting the last write-ahead log location after being demoted"

#define COMMENT_REPORT_LSN_TO_PREP_PROMOTION \
	"Stop traffic to primary, " \
	"wait for it to finish draining."

#define COMMENT_REPORT_LSN_TO_FAST_FORWARD \
	"Fetching missing WAL bits from another standby before promotion"

#define COMMENT_REPORT_LSN_TO_SINGLE \
	"There is no other node anymore, promote this node"

#define COMMENT_WAIT_MAINTENANCE_TO_SINGLE \
	"Was waiting to be sent to maintenance, but the primary vanished, " \
	"promote this node"

#define COMMENT_FAST_FORWARD_TO_SINGLE \
	"Was fetching missing WAL from another standby, but every other node " \
	"vanished, promote this node with whatever it has"

#define COMMENT_FOLLOW_NEW_PRIMARY \
	"Switch replication to the new primary"

#define COMMENT_REPORT_LSN_TO_JOIN_SECONDARY \
	"A failover candidate has been selected, stop replication"

#define COMMENT_JOIN_SECONDARY_TO_SECONDARY \
	"Failover is done, we have a new primary to follow"

#define COMMENT_FAST_FORWARD_TO_PREP_PROMOTION \
	"Got the missing WAL bytes, promoted"

#define COMMENT_INIT_TO_REPORT_LSN \
	"Creating a new node from a standby node that is not a candidate."

#define COMMENT_DROPPED_TO_REPORT_LSN \
	"This node is being reinitialized after having been dropped"

#define COMMENT_ANY_TO_DROPPED \
	"This node is being dropped from the monitor"


/*
 * The full 2-nodes state machine contains states that are expected only when
 * the node is a primary, and some only when the node is a standby. Each node
 * is going to change role in its life-cycle, so having the whole life-cycle in
 * a single FSM makes sense.
 *
 * The FSM is normally driven by an external node, the monitor. See design
 * docs.
 */
KeeperFSMTransition KeeperFSM[] = {
	/*
	 * CURRENT_STATE, ASSIGNED_STATE, NODE_KIND,
	 * COMMENT,
	 * TRANSTION_FUNCTION
	 */

	/*
	 * Started as a single, no nothing
	 */
	{
		INIT_STATE, SINGLE_STATE, NODE_KIND_CITUS_COORDINATOR,
		COMMENT_INIT_TO_SINGLE,
		&fsm_citus_coordinator_init_primary
	},

	{
		INIT_STATE, SINGLE_STATE, NODE_KIND_CITUS_WORKER,
		COMMENT_INIT_TO_SINGLE,
		&fsm_citus_worker_init_primary
	},

	{
		INIT_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_INIT_TO_SINGLE,
		&fsm_init_primary,
		FSM_PHASE_INIT
	},

	{
		DROPPED_STATE, SINGLE_STATE, NODE_KIND_CITUS_WORKER,
		COMMENT_INIT_TO_SINGLE,
		&fsm_citus_worker_init_primary
	},

	{
		DROPPED_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_INIT_TO_SINGLE,
		&fsm_init_primary,
		FSM_PHASE_INIT
	},

	{
		DROPPED_STATE, REPORT_LSN_STATE, NODE_KIND_ANY,
		COMMENT_DROPPED_TO_REPORT_LSN,
		&fsm_init_from_standby,
		FSM_PHASE_INIT
	},

	/*
	 * other node(s) was forcibly removed, now single
	 */
	{
		PRIMARY_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_SINGLE,
		&fsm_disable_replication,
		FSM_PHASE_REMOVAL
	},

	{
		WAIT_PRIMARY_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_SINGLE,
		&fsm_disable_replication,
		FSM_PHASE_REMOVAL
	},

	{
		JOIN_PRIMARY_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_SINGLE,
		&fsm_disable_replication,
		FSM_PHASE_REMOVAL
	},

	/*
	 * failover occurred, primary -> draining/demoted
	 */
	{
		PRIMARY_STATE, DRAINING_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_DRAINING,
		&fsm_stop_postgres,
		FSM_PHASE_FAILOVER
	},

	{
		DRAINING_STATE, DEMOTED_STATE, NODE_KIND_ANY,
		COMMENT_DRAINING_TO_DEMOTED,
		&fsm_stop_postgres,
		FSM_PHASE_FAILOVER
	},

	{
		PRIMARY_STATE, DEMOTED_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_DEMOTED,
		&fsm_stop_postgres,
		FSM_PHASE_FAILOVER
	},

	{
		PRIMARY_STATE, DEMOTE_TIMEOUT_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_DEMOTED,
		&fsm_stop_postgres,
		FSM_PHASE_FAILOVER
	},

	{
		JOIN_PRIMARY_STATE, DRAINING_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_DRAINING,
		&fsm_stop_postgres,
		FSM_PHASE_FAILOVER
	},

	{
		JOIN_PRIMARY_STATE, DEMOTED_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_DEMOTED,
		&fsm_stop_postgres,
		FSM_PHASE_FAILOVER
	},

	{
		JOIN_PRIMARY_STATE, DEMOTE_TIMEOUT_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_DEMOTED,
		&fsm_stop_postgres,
		FSM_PHASE_FAILOVER
	},

	{
		APPLY_SETTINGS_STATE, DRAINING_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_DRAINING,
		&fsm_stop_postgres,
		FSM_PHASE_FAILOVER
	},

	{
		APPLY_SETTINGS_STATE, DEMOTED_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_DEMOTED,
		&fsm_stop_postgres,
		FSM_PHASE_FAILOVER
	},

	{
		APPLY_SETTINGS_STATE, DEMOTE_TIMEOUT_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_DEMOTED,
		&fsm_stop_postgres,
		FSM_PHASE_FAILOVER
	},

	/*
	 * primary is put to maintenance
	 */
	{
		PRIMARY_STATE, PREPARE_MAINTENANCE_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_PREPARE_MAINTENANCE,
		&fsm_stop_postgres_for_primary_maintenance,
		FSM_PHASE_MAINTENANCE
	},

	{
		PREPARE_MAINTENANCE_STATE, MAINTENANCE_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_MAINTENANCE,
		&fsm_stop_postgres_and_setup_standby,
		FSM_PHASE_MAINTENANCE
	},

	{
		PRIMARY_STATE, MAINTENANCE_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_MAINTENANCE,
		&fsm_stop_postgres_for_primary_maintenance,
		FSM_PHASE_MAINTENANCE
	},

	/*
	 * was demoted, need to be dead now.
	 */
	{
		DRAINING_STATE, DEMOTE_TIMEOUT_STATE, NODE_KIND_ANY,
		COMMENT_DRAINING_TO_DEMOTE_TIMEOUT,
		&fsm_stop_postgres,
		FSM_PHASE_FAILOVER
	},

	{
		DEMOTE_TIMEOUT_STATE, DEMOTED_STATE, NODE_KIND_ANY,
		COMMENT_DEMOTE_TIMEOUT_TO_DEMOTED,
		&fsm_stop_postgres,
		FSM_PHASE_FAILOVER
	},

	/*
	 * wait_primary stops reporting, is (supposed) dead now
	 */
	{
		WAIT_PRIMARY_STATE, DEMOTED_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_DEMOTED,
		&fsm_stop_postgres,
		FSM_PHASE_FAILOVER
	},

	/*
	 * was demoted after a failure, but standby was forcibly removed
	 */
	{
		DEMOTED_STATE, SINGLE_STATE, NODE_KIND_CITUS_WORKER,
		COMMENT_DEMOTED_TO_SINGLE,
		&fsm_citus_worker_resume_as_primary
	},

	{
		DEMOTED_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_DEMOTED_TO_SINGLE,
		&fsm_resume_as_primary,
		FSM_PHASE_REMOVAL
	},

	{
		DEMOTE_TIMEOUT_STATE, SINGLE_STATE, NODE_KIND_CITUS_WORKER,
		COMMENT_DEMOTED_TO_SINGLE,
		&fsm_citus_worker_resume_as_primary
	},

	{
		DEMOTE_TIMEOUT_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_DEMOTED_TO_SINGLE,
		&fsm_resume_as_primary,
		FSM_PHASE_REMOVAL
	},

	{
		DRAINING_STATE, SINGLE_STATE, NODE_KIND_CITUS_WORKER,
		COMMENT_DEMOTED_TO_SINGLE,
		&fsm_citus_worker_resume_as_primary
	},

	{
		DRAINING_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_DEMOTED_TO_SINGLE,
		&fsm_resume_as_primary,
		FSM_PHASE_REMOVAL
	},

	/*
	 * primary was forcibly removed
	 */
	{
		SECONDARY_STATE, SINGLE_STATE, NODE_KIND_CITUS_COORDINATOR,
		COMMENT_LOST_PRIMARY,
		&fsm_citus_coordinator_promote_standby_to_single
	},

	{
		SECONDARY_STATE, SINGLE_STATE, NODE_KIND_CITUS_WORKER,
		COMMENT_LOST_PRIMARY,
		&fsm_citus_worker_promote_standby_to_single
	},

	{
		SECONDARY_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_LOST_PRIMARY,
		&fsm_promote_standby,
		FSM_PHASE_REMOVAL
	},

	{
		CATCHINGUP_STATE, SINGLE_STATE, NODE_KIND_CITUS_COORDINATOR,
		COMMENT_LOST_PRIMARY,
		&fsm_citus_coordinator_promote_standby_to_single
	},

	{
		CATCHINGUP_STATE, SINGLE_STATE, NODE_KIND_CITUS_WORKER,
		COMMENT_LOST_PRIMARY,
		&fsm_citus_worker_promote_standby_to_single
	},

	{
		CATCHINGUP_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_LOST_PRIMARY,
		&fsm_promote_standby,
		FSM_PHASE_REMOVAL
	},

	{
		PREP_PROMOTION_STATE, SINGLE_STATE, NODE_KIND_CITUS_COORDINATOR,
		COMMENT_LOST_PRIMARY,
		&fsm_citus_coordinator_promote_standby_to_single
	},

	{
		PREP_PROMOTION_STATE, SINGLE_STATE, NODE_KIND_CITUS_WORKER,
		COMMENT_LOST_PRIMARY,
		&fsm_citus_worker_promote_standby_to_single
	},

	{
		PREP_PROMOTION_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_LOST_PRIMARY,
		&fsm_promote_standby,
		FSM_PHASE_REMOVAL
	},

	/*
	 * went down to force the primary to time out, but then it was removed
	 */
	{
		STOP_REPLICATION_STATE, SINGLE_STATE, NODE_KIND_CITUS_WORKER,
		COMMENT_REPLICATION_TO_SINGLE,
		&fsm_citus_worker_promote_standby_to_single
	},

	{
		STOP_REPLICATION_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_REPLICATION_TO_SINGLE,
		&fsm_promote_standby,
		FSM_PHASE_REMOVAL
	},

	/*
	 * all states should lead to SINGLE, including REPORT_LSN
	 */
	{
		REPORT_LSN_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_REPORT_LSN_TO_SINGLE,
		&fsm_promote_standby,
		FSM_PHASE_REMOVAL
	},

	/*
	 * was waiting for the primary to disable sync replication before going
	 * to maintenance (a converged, actively-streaming standby -- entering
	 * WAIT_MAINTENANCE_STATE itself runs no transition function, see its own
	 * comment below), but the primary was forcibly removed instead: reuse
	 * fsm_promote_standby exactly like every other converged-standby source
	 * state above (SECONDARY/CATCHINGUP/PREP_PROMOTION/STOP_REPLICATION/
	 * REPORT_LSN), since Postgres here is already running and replicating,
	 * with no special handling wait_maintenance itself needs.
	 */
	{
		WAIT_MAINTENANCE_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_WAIT_MAINTENANCE_TO_SINGLE,
		&fsm_promote_standby,
		FSM_PHASE_REMOVAL
	},

	/*
	 * was fetching missing WAL from another standby to catch up before
	 * promotion (a converged-enough standby -- Postgres is already running
	 * and replicating, same shape as the other converged-standby source
	 * states above), but every other node vanished, including the peer we
	 * were fetching from: fsm_fast_forward's own "no upstream found" branch
	 * already accepts this (skips the fetch, treats local data as the best
	 * available -- there is nothing more advanced left anywhere to lose),
	 * so promoting with whatever this node already has is exactly as safe
	 * as it gets. Reuse fsm_promote_standby exactly like every other
	 * converged-standby source state -- no separate WAL fetch is attempted
	 * or needed here, matching PREP_PROMOTION_STATE/STOP_REPLICATION_STATE's
	 * own direct-to-SINGLE shortcut for "was mid-promotion, peer vanished".
	 */
	{
		FAST_FORWARD_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_FAST_FORWARD_TO_SINGLE,
		&fsm_promote_standby,
		FSM_PHASE_REMOVAL
	},

	/*
	 * On the Primary, wait for a standby to be ready: WAIT_PRIMARY
	 */
	{
		SINGLE_STATE, WAIT_PRIMARY_STATE, NODE_KIND_ANY,
		COMMENT_SINGLE_TO_WAIT_PRIMARY,
		&fsm_prepare_replication,
		FSM_PHASE_INIT
	},

	{
		PRIMARY_STATE, JOIN_PRIMARY_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_JOIN_PRIMARY,
		&fsm_prepare_replication,
		FSM_PHASE_STEADY_STATE
	},

	{
		PRIMARY_STATE, WAIT_PRIMARY_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_WAIT_PRIMARY,
		&fsm_disable_sync_rep,
		FSM_PHASE_STEADY_STATE
	},

	{
		JOIN_PRIMARY_STATE, WAIT_PRIMARY_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_WAIT_PRIMARY,
		&fsm_disable_sync_rep,
		FSM_PHASE_STEADY_STATE
	},

	{
		WAIT_PRIMARY_STATE, JOIN_PRIMARY_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_JOIN_PRIMARY,
		&fsm_prepare_replication,
		FSM_PHASE_STEADY_STATE
	},

	/*
	 * Situation is getting back to normal on the primary
	 */
	{
		WAIT_PRIMARY_STATE, PRIMARY_STATE, NODE_KIND_ANY,
		COMMENT_WAIT_PRIMARY_TO_PRIMARY,
		&fsm_enable_sync_rep,
		FSM_PHASE_STEADY_STATE
	},

	{
		JOIN_PRIMARY_STATE, PRIMARY_STATE, NODE_KIND_ANY,
		COMMENT_JOIN_PRIMARY_TO_PRIMARY,
		&fsm_enable_sync_rep,
		FSM_PHASE_STEADY_STATE
	},

	{
		DEMOTE_TIMEOUT_STATE, PRIMARY_STATE, NODE_KIND_ANY,
		COMMENT_DEMOTE_TO_PRIMARY,
		&fsm_start_postgres,
		FSM_PHASE_FAILOVER
	},

	/*
	 * The primary is now ready to accept a standby, we're the standby
	 */
	{
		WAIT_STANDBY_STATE, CATCHINGUP_STATE, NODE_KIND_ANY,
		COMMENT_WAIT_STANDBY_TO_CATCHINGUP,
		&fsm_init_standby,
		FSM_PHASE_INIT
	},

	{
		DEMOTED_STATE, CATCHINGUP_STATE, NODE_KIND_ANY,
		COMMENT_DEMOTED_TO_CATCHINGUP,
		&fsm_rewind_or_init,
		FSM_PHASE_FAILOVER
	},

	{
		SECONDARY_STATE, CATCHINGUP_STATE, NODE_KIND_ANY,
		COMMENT_SECONDARY_TO_CATCHINGUP,
		&fsm_follow_new_primary,
		FSM_PHASE_STEADY_STATE
	},

	/*
	 * We're asked to be a standby.
	 */
	{
		CATCHINGUP_STATE, SECONDARY_STATE, NODE_KIND_CITUS_ANY,
		COMMENT_CATCHINGUP_TO_SECONDARY,
		&fsm_citus_maintain_replication_slots
	},

	{
		CATCHINGUP_STATE, SECONDARY_STATE, NODE_KIND_ANY,
		COMMENT_CATCHINGUP_TO_SECONDARY,
		&fsm_prepare_for_secondary,
		FSM_PHASE_STEADY_STATE
	},

	/*
	 * The standby is asked to prepare its own promotion
	 */
	{
		SECONDARY_STATE, PREP_PROMOTION_STATE, NODE_KIND_CITUS_WORKER,
		COMMENT_SECONDARY_TO_PREP_PROMOTION,
		&fsm_citus_worker_prepare_standby_for_promotion
	},

	{
		SECONDARY_STATE, PREP_PROMOTION_STATE, NODE_KIND_ANY,
		COMMENT_SECONDARY_TO_PREP_PROMOTION,
		&fsm_prepare_standby_for_promotion,
		FSM_PHASE_FAILOVER
	},

	{
		CATCHINGUP_STATE, PREP_PROMOTION_STATE, NODE_KIND_CITUS_WORKER,
		COMMENT_SECONDARY_TO_PREP_PROMOTION,
		&fsm_citus_worker_prepare_standby_for_promotion
	},

	{
		CATCHINGUP_STATE, PREP_PROMOTION_STATE, NODE_KIND_ANY,
		COMMENT_SECONDARY_TO_PREP_PROMOTION,
		&fsm_prepare_standby_for_promotion,
		FSM_PHASE_FAILOVER
	},

	/*
	 * Forcefully stop replication by stopping the server.
	 */
	{
		PREP_PROMOTION_STATE, STOP_REPLICATION_STATE, NODE_KIND_CITUS_WORKER,
		COMMENT_PROMOTION_TO_STOP_REPLICATION,
		&fsm_citus_worker_stop_replication
	},

	{
		PREP_PROMOTION_STATE, STOP_REPLICATION_STATE, NODE_KIND_ANY,
		COMMENT_PROMOTION_TO_STOP_REPLICATION,
		&fsm_stop_replication,
		FSM_PHASE_FAILOVER
	},

	/*
	 * finish the promotion
	 */
	{
		STOP_REPLICATION_STATE, WAIT_PRIMARY_STATE, NODE_KIND_CITUS_COORDINATOR,
		COMMENT_STOP_REPLICATION_TO_WAIT_PRIMARY,
		&fsm_citus_coordinator_promote_standby_to_primary
	},

	{
		STOP_REPLICATION_STATE, WAIT_PRIMARY_STATE, NODE_KIND_CITUS_WORKER,
		COMMENT_STOP_REPLICATION_TO_WAIT_PRIMARY,
		&fsm_citus_worker_promote_standby_to_primary
	},

	{
		STOP_REPLICATION_STATE, WAIT_PRIMARY_STATE, NODE_KIND_ANY,
		COMMENT_STOP_REPLICATION_TO_WAIT_PRIMARY,
		&fsm_promote_standby_to_primary,
		FSM_PHASE_FAILOVER
	},

	{
		PREP_PROMOTION_STATE, WAIT_PRIMARY_STATE, NODE_KIND_CITUS_WORKER,
		COMMENT_BLOCKED_WRITES,
		&fsm_citus_worker_promote_standby
	},

	{
		PREP_PROMOTION_STATE, WAIT_PRIMARY_STATE, NODE_KIND_ANY,
		COMMENT_BLOCKED_WRITES,
		&fsm_promote_standby,
		FSM_PHASE_FAILOVER
	},

	/*
	 * Just wait until primary is ready
	 */
	{
		INIT_STATE, WAIT_STANDBY_STATE, NODE_KIND_ANY,
		COMMENT_INIT_TO_WAIT_STANDBY,
		NULL,
		FSM_PHASE_INIT
	},

	{
		DROPPED_STATE, WAIT_STANDBY_STATE, NODE_KIND_ANY,
		COMMENT_INIT_TO_WAIT_STANDBY,
		NULL,
		FSM_PHASE_INIT
	},

	/*
	 * When losing a monitor and then connecting to a new monitor as a
	 * secondary, we need to be able to follow the init sequence again.
	 */
	{
		SECONDARY_STATE, WAIT_STANDBY_STATE, NODE_KIND_ANY,
		COMMENT_SECONARY_TO_WAIT_STANDBY,
		NULL,
		FSM_PHASE_STEADY_STATE
	},

	/*
	 * In case of maintenance of the standby server, we stop PostgreSQL.
	 */
	{
		SECONDARY_STATE, WAIT_MAINTENANCE_STATE, NODE_KIND_ANY,
		COMMENT_SECONDARY_TO_WAIT_MAINTENANCE,
		NULL,
		FSM_PHASE_MAINTENANCE
	},

	{
		CATCHINGUP_STATE, WAIT_MAINTENANCE_STATE, NODE_KIND_ANY,
		COMMENT_SECONDARY_TO_WAIT_MAINTENANCE,
		NULL,
		FSM_PHASE_MAINTENANCE
	},

	{
		SECONDARY_STATE, MAINTENANCE_STATE, NODE_KIND_ANY,
		COMMENT_SECONDARY_TO_MAINTENANCE,
		&fsm_start_maintenance_on_standby,
		FSM_PHASE_MAINTENANCE
	},

	{
		CATCHINGUP_STATE, MAINTENANCE_STATE, NODE_KIND_ANY,
		COMMENT_SECONDARY_TO_MAINTENANCE,
		&fsm_start_maintenance_on_standby,
		FSM_PHASE_MAINTENANCE
	},

	{
		WAIT_MAINTENANCE_STATE, MAINTENANCE_STATE, NODE_KIND_ANY,
		COMMENT_SECONDARY_TO_MAINTENANCE,
		&fsm_start_maintenance_on_standby,
		FSM_PHASE_MAINTENANCE
	},

	{
		MAINTENANCE_STATE, CATCHINGUP_STATE, NODE_KIND_ANY,
		COMMENT_MAINTENANCE_TO_CATCHINGUP,
		&fsm_restart_standby,
		FSM_PHASE_MAINTENANCE
	},

	{
		PREPARE_MAINTENANCE_STATE, CATCHINGUP_STATE, NODE_KIND_ANY,
		COMMENT_MAINTENANCE_TO_CATCHINGUP,
		&fsm_restart_standby,
		FSM_PHASE_MAINTENANCE
	},

	/*
	 * Applying new replication/cluster settings (per node replication quorum,
	 * candidate priorities, or per formation number_sync_standbys) means we
	 * have to fetch the new value for synchronous_standby_names from the
	 * monitor.
	 */
	{
		PRIMARY_STATE, APPLY_SETTINGS_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_APPLY_SETTINGS,
		NULL,
		FSM_PHASE_STEADY_STATE
	},

	{
		WAIT_PRIMARY_STATE, APPLY_SETTINGS_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_APPLY_SETTINGS,
		NULL,
		FSM_PHASE_STEADY_STATE
	},

	{
		APPLY_SETTINGS_STATE, PRIMARY_STATE, NODE_KIND_ANY,
		COMMENT_APPLY_SETTINGS_TO_PRIMARY,
		&fsm_enable_sync_rep,
		FSM_PHASE_STEADY_STATE
	},

	{
		APPLY_SETTINGS_STATE, SINGLE_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_SINGLE,
		&fsm_disable_replication,
		FSM_PHASE_REMOVAL
	},

	{
		APPLY_SETTINGS_STATE, WAIT_PRIMARY_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_WAIT_PRIMARY,
		&fsm_disable_sync_rep,
		FSM_PHASE_STEADY_STATE
	},

	{
		APPLY_SETTINGS_STATE, JOIN_PRIMARY_STATE, NODE_KIND_ANY,
		COMMENT_PRIMARY_TO_JOIN_PRIMARY,
		&fsm_prepare_replication,
		FSM_PHASE_STEADY_STATE
	},

	/*
	 * In case of multiple standbys, failover begins with reporting current LSN
	 */

	{
		SECONDARY_STATE, REPORT_LSN_STATE, NODE_KIND_ANY,
		COMMENT_SECONDARY_TO_REPORT_LSN,
		&fsm_report_lsn,
		FSM_PHASE_FAILOVER
	},

	{
		CATCHINGUP_STATE, REPORT_LSN_STATE, NODE_KIND_ANY,
		COMMENT_SECONDARY_TO_REPORT_LSN,
		&fsm_report_lsn,
		FSM_PHASE_FAILOVER
	},

	{
		MAINTENANCE_STATE, REPORT_LSN_STATE, NODE_KIND_ANY,
		COMMENT_SECONDARY_TO_REPORT_LSN,
		&fsm_report_lsn,
		FSM_PHASE_MAINTENANCE
	},

	{
		PREPARE_MAINTENANCE_STATE, REPORT_LSN_STATE, NODE_KIND_ANY,
		COMMENT_SECONDARY_TO_REPORT_LSN,
		&fsm_report_lsn,
		FSM_PHASE_MAINTENANCE
	},

	/*
	 * was waiting for the primary to disable sync replication before going
	 * to maintenance (a converged, actively-streaming standby, same as the
	 * other reused source states just above), but the primary was forcibly
	 * removed and this node's own candidate-priority is 0 -- reuse
	 * fsm_report_lsn exactly like SECONDARY/CATCHINGUP/MAINTENANCE/
	 * PREPARE_MAINTENANCE above.
	 */
	{
		WAIT_MAINTENANCE_STATE, REPORT_LSN_STATE, NODE_KIND_ANY,
		COMMENT_SECONDARY_TO_REPORT_LSN,
		&fsm_report_lsn,
		FSM_PHASE_MAINTENANCE
	},

	/*
	 * was fetching missing WAL from another standby to catch up before
	 * promotion (same converged-enough shape as the source states above),
	 * but every other node vanished and this node's own candidate-priority
	 * is 0 -- reuse fsm_report_lsn exactly like SECONDARY/CATCHINGUP/
	 * MAINTENANCE/PREPARE_MAINTENANCE/WAIT_MAINTENANCE above. No WAL fetch
	 * is attempted here either, same reasoning as FAST_FORWARD_STATE ->
	 * SINGLE_STATE's own comment.
	 */
	{
		FAST_FORWARD_STATE, REPORT_LSN_STATE, NODE_KIND_ANY,
		COMMENT_SECONDARY_TO_REPORT_LSN,
		&fsm_report_lsn,
		FSM_PHASE_FAILOVER
	},

	{
		REPORT_LSN_STATE, PREP_PROMOTION_STATE, NODE_KIND_CITUS_WORKER,
		COMMENT_REPORT_LSN_TO_PREP_PROMOTION,
		&fsm_citus_worker_prepare_standby_for_promotion
	},

	{
		REPORT_LSN_STATE, PREP_PROMOTION_STATE, NODE_KIND_ANY,
		COMMENT_REPORT_LSN_TO_PREP_PROMOTION,
		&fsm_prepare_standby_for_promotion,
		FSM_PHASE_FAILOVER
	},

	{
		REPORT_LSN_STATE, FAST_FORWARD_STATE, NODE_KIND_ANY,
		COMMENT_REPORT_LSN_TO_FAST_FORWARD,
		&fsm_fast_forward,
		FSM_PHASE_FAILOVER
	},

	{
		FAST_FORWARD_STATE, PREP_PROMOTION_STATE, NODE_KIND_CITUS_ANY,
		COMMENT_FAST_FORWARD_TO_PREP_PROMOTION,
		&fsm_citus_cleanup_and_resume_as_primary
	},

	{
		FAST_FORWARD_STATE, PREP_PROMOTION_STATE, NODE_KIND_ANY,
		COMMENT_FAST_FORWARD_TO_PREP_PROMOTION,
		&fsm_cleanup_as_primary,
		FSM_PHASE_FAILOVER
	},

	{
		REPORT_LSN_STATE, JOIN_SECONDARY_STATE, NODE_KIND_ANY,
		COMMENT_REPORT_LSN_TO_JOIN_SECONDARY,
		&fsm_checkpoint_and_stop_postgres,
		FSM_PHASE_FAILOVER
	},

	{
		REPORT_LSN_STATE, SECONDARY_STATE, NODE_KIND_ANY,
		COMMENT_REPORT_LSN_TO_JOIN_SECONDARY,
		&fsm_follow_new_primary,
		FSM_PHASE_FAILOVER
	},

	{
		JOIN_SECONDARY_STATE, SECONDARY_STATE, NODE_KIND_ANY,
		COMMENT_JOIN_SECONDARY_TO_SECONDARY,
		&fsm_follow_new_primary,
		FSM_PHASE_FAILOVER
	},

	/*
	 * When an old primary gets back online and reaches draining/draining, if a
	 * failover is on-going then have it join the selection process.
	 */
	{
		DRAINING_STATE, REPORT_LSN_STATE, NODE_KIND_ANY,
		COMMENT_DRAINING_TO_REPORT_LSN,
		&fsm_report_lsn_and_drop_replication_slots,
		FSM_PHASE_FAILOVER
	},

	{
		DEMOTED_STATE, REPORT_LSN_STATE, NODE_KIND_ANY,
		COMMENT_DEMOTED_TO_REPORT_LSN,
		&fsm_report_lsn_and_drop_replication_slots,
		FSM_PHASE_FAILOVER
	},

	/*
	 * When adding a new node and there is no primary, but there are existing
	 * nodes that are not candidates for failover.
	 */
	{
		INIT_STATE, REPORT_LSN_STATE, NODE_KIND_ANY,
		COMMENT_INIT_TO_REPORT_LSN,
		&fsm_init_from_standby,
		FSM_PHASE_INIT
	},

	/*
	 * Dropping a node is a two-step process
	 */
	{
		ANY_STATE, DROPPED_STATE, NODE_KIND_CITUS_ANY,
		COMMENT_ANY_TO_DROPPED,
		&fsm_citus_drop_node
	},

	{
		ANY_STATE, DROPPED_STATE, NODE_KIND_ANY,
		COMMENT_ANY_TO_DROPPED,
		&fsm_drop_node,
		FSM_PHASE_REMOVAL
	},

	/*
	 * This is the end, my friend.
	 */
	{
		NO_STATE, NO_STATE, NODE_KIND_ANY,
		NULL,
		NULL
	},
};


/*
 * keeper_fsm_step implements the logic to perform a single step
 * of the state machine according to the goal state returned by
 * the monitor.
 */
bool
keeper_fsm_step(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	KeeperStateData *keeperState = &(keeper->state);
	Monitor *monitor = &(keeper->monitor);
	LocalPostgresServer *postgres = &(keeper->postgres);
	MonitorAssignedState assignedState = { 0 };

	/*
	 * Update our in-memory representation of PostgreSQL state, ignore errors
	 * as in the main loop: we continue with default WAL lag of -1 and an empty
	 * string for pgsrSyncState.
	 */
	(void) keeper_update_pg_state(keeper, LOG_DEBUG);

	log_debug("Calling node_active for node %s/%d/%d with current state: "
			  "PostgreSQL is running is %s, "
			  "sync_state is \"%s\", "
			  "latest WAL LSN is %s.",
			  config->formation,
			  keeperState->current_node_id,
			  keeperState->current_group,
			  postgres->pgIsRunning ? "true" : "false",
			  postgres->pgsrSyncState,
			  postgres->currentLSN);

	if (!monitor_node_active(monitor,
							 config->formation,
							 keeperState->current_node_id,
							 keeperState->current_group,
							 keeperState->current_role,
							 postgres->pgIsRunning,
							 postgres->postgresSetup.control.timeline_id,
							 postgres->currentLSN,
							 postgres->pgsrSyncState,
							 &assignedState))
	{
		log_fatal("Failed to get the goal state from the monitor, "
				  "see above for details");
		return false;
	}

	/*
	 * Assign the new state. We skip writing the state file here since we can
	 * (and should) always get the assigned state from the monitor.
	 */
	keeperState->assigned_role = assignedState.state;

	/* roll the state machine forward */
	if (keeperState->assigned_role != keeperState->current_role)
	{
		if (!keeper_fsm_reach_assigned_state(keeper))
		{
			/* errors have already been logged */
			return false;
		}
	}
	else
	{
		/*
		 * Now that we know if PostgreSQL is running or not, maybe restart it,
		 * or maybe shut it down, depending on what the current state expects.
		 */
		if (!keeper_ensure_current_state(keeper))
		{
			log_warn("pg_autoctl keeper failed to ensure current state \"%s\": "
					 "PostgreSQL %s running",
					 NodeStateToString(keeperState->current_role),
					 postgres->pgIsRunning ? "is" : "is not");
		}
	}

	/* update state file */
	if (!keeper_update_state(keeper, assignedState.nodeId, assignedState.groupId,
							 assignedState.state, true))
	{
		log_error("Failed to write keepers state file, see above for details");
		return false;
	}

	return true;
}


/*
 * keeper_fsm_step_report implements only the first half of keeper_fsm_step:
 * report our own current_role to the monitor via node_active() and persist
 * whatever new assigned_role it hands back, but never attempt the local
 * transition toward it. Unlike keeper_fsm_step (report + attempt-transition,
 * atomically, in one call -- the ordinary autopilot shape), this lets a
 * pgaftest spec observe "the monitor has assigned a new goal" as its own,
 * separate, externally-controlled moment, with current_role held frozen at
 * whatever it already was -- see keeper_fsm_step_advance's own comment for
 * why that separation is what several MonitorFSM[] gap-closing test specs
 * actually need: freezing a node's own reportedState while a fan-out from a
 * *different* node's own report bumps this one's goalState is not otherwise
 * observable, since keeper_fsm_step's own combined call would immediately
 * re-converge past whatever intermediate state a spec wants to hold it at.
 */
bool
keeper_fsm_step_report(Keeper *keeper)
{
	KeeperStateData *keeperState = &(keeper->state);
	Monitor *monitor = &(keeper->monitor);
	LocalPostgresServer *postgres = &(keeper->postgres);
	MonitorAssignedState assignedState = { 0 };

	(void) keeper_update_pg_state(keeper, LOG_DEBUG);

	if (!monitor_node_active(monitor,
							 keeper->config.formation,
							 keeperState->current_node_id,
							 keeperState->current_group,
							 keeperState->current_role,
							 postgres->pgIsRunning,
							 postgres->postgresSetup.control.timeline_id,
							 postgres->currentLSN,
							 postgres->pgsrSyncState,
							 &assignedState))
	{
		log_fatal("Failed to get the goal state from the monitor, "
				  "see above for details");
		return false;
	}

	keeperState->assigned_role = assignedState.state;

	if (!keeper_update_state(keeper, assignedState.nodeId, assignedState.groupId,
							 assignedState.state, true))
	{
		log_error("Failed to write keepers state file, see above for details");
		return false;
	}

	return true;
}


/*
 * keeper_fsm_step_advance implements only the second half of
 * keeper_fsm_step: attempt the local transition from our own current_role
 * to whatever assigned_role is already on file (typically from an earlier
 * keeper_fsm_step_report call, possibly several node_active() calls to
 * *other* nodes ago) -- no monitor round trip at all. Kept deliberately as
 * thin a wrapper as possible around keeper_fsm_reach_assigned_state (which
 * already updates current_role on success): the only other work here is
 * persisting the result, via keeper_store_state rather than
 * keeper_update_state, since there's no freshly-returned MonitorAssignedState
 * to merge in (see the difference in what each caller has on hand between
 * cli_do_fsm.c's own two call sites).
 */
bool
keeper_fsm_step_advance(Keeper *keeper)
{
	if (!keeper_fsm_reach_assigned_state(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	if (!keeper_store_state(keeper))
	{
		log_error("Failed to write keepers state file, see above for details");
		return false;
	}

	return true;
}


/*
 * keeper_fsm_reach_assigned_state uses the KeeperFSM to drive a transition
 * from keeper->state->current_role to keeper->state->assigned_role, when
 * that's supported.
 */
bool
keeper_fsm_reach_assigned_state(Keeper *keeper)
{
	int transitionIndex = 0;
	KeeperStateData *keeperState = &(keeper->state);
	KeeperFSMTransition transition = KeeperFSM[0];

	if (keeperState->current_role == keeperState->assigned_role)
	{
		log_debug("Current state and Goal state are the same (\"%s\").",
				  NodeStateToString(keeperState->current_role));

		return true;
	}

	while (transition.current != NO_STATE)
	{
		if (state_matches(transition.current, keeperState->current_role) &&
			state_matches(transition.assigned, keeperState->assigned_role) &&
			pgKind_matches(transition.pgKind, keeper->config.pgSetup.pgKind))
		{
			bool ret = false;

			/* avoid logging "#any state#" to the user */
			if (transition.current != ANY_STATE)
			{
				log_info("FSM transition from \"%s\" to \"%s\"%s%s",
						 NodeStateToString(transition.current),
						 NodeStateToString(transition.assigned),
						 transition.comment ? ": " : "",
						 transition.comment ? transition.comment : "");
			}
			else
			{
				log_info("FSM transition to \"%s\"%s%s",
						 NodeStateToString(transition.assigned),
						 transition.comment ? ": " : "",
						 transition.comment ? transition.comment : "");
			}

			if (transition.transitionFunction)
			{
				ret = (*transition.transitionFunction)(keeper);

				log_debug("Transition function returned: %s",
						  ret ? "true" : "false");
			}
			else
			{
				ret = true;
				log_debug("No transition function, assigning new state");
			}

			if (ret)
			{
				keeperState->current_role = keeperState->assigned_role;

				log_info("Transition complete: current state is now \"%s\"",
						 NodeStateToString(keeperState->current_role));
			}
			else
			{
				/* avoid logging "#any state#" to the user */
				if (transition.current != ANY_STATE)
				{
					log_error("Failed to transition from state \"%s\" "
							  "to state \"%s\", see above.",
							  NodeStateToString(transition.current),
							  NodeStateToString(transition.assigned));
				}
				else
				{
					log_error("Failed to transition to state \"%s\", see above.",
							  NodeStateToString(transition.assigned));
				}
			}

			return ret;
		}
		transition = KeeperFSM[++transitionIndex];
	}

	/*
	 * we didn't find a transition
	 */
	log_fatal("pg_autoctl does not know how to reach state \"%s\" from \"%s\"",
			  NodeStateToString(keeperState->assigned_role),
			  NodeStateToString(keeperState->current_role));

	return false;
}


/*
 * print_reachable_states shows the list of states we can reach using the FSM
 * transitions from KeeperState.current_role.
 */
void
print_reachable_states(KeeperStateData *keeperState)
{
	int transitionIndex = 0;
	bool header = false;
	KeeperFSMTransition transition = KeeperFSM[0];

	log_debug("print_reachable_states: %s",
			  NodeStateToString(keeperState->current_role));

	while (transition.current != NO_STATE)
	{
		if (state_matches(transition.current, keeperState->current_role))
		{
			if (!header)
			{
				fformat(stdout, "%20s | %20s | %s\n",
						"Current", "Reachable", "Comment");
				fformat(stdout, "%20s-+-%20s-+-%s\n",
						"--------------------",
						"--------------------",
						"--------------------");
				header = true;
			}
			fformat(stdout,
					"%20s | %20s | %s\n",
					NodeStateToString(transition.current),
					NodeStateToString(transition.assigned),
					transition.comment);
		}
		transition = KeeperFSM[++transitionIndex];
	}
}


/*
 * print_fsm_for_graphviz outputs the program used by graphviz to draw a visual
 * representation of our state machine.
 *
 *   pg_autoctl do fsm gv | dot -Tpng > fsm.png
 */
void
print_fsm_for_graphviz(void)
{
	KeeperFSMTransition transition = KeeperFSM[0];
	int transitionIndex = 0;

	fformat(
		stdout,
		"digraph finite_state_machine\n"
		"{\n"
		"    size=\"12\"\n"
		"    ratio=\"fill\"\n"
		"    node [shape = doubleoctagon, style=filled, color=\"bisque1\"]; init primary secondary; \n"
		"    node [shape = octagon, style=filled color=\"bisque3\"]; \n");

	while (transition.current != NO_STATE)
	{
		fformat(stdout,
				"    %s -> %s [ label = \"%s\" ];\n",
				NodeStateToString(transition.current),
				NodeStateToString(transition.assigned),
				transition.comment);

		transition = KeeperFSM[++transitionIndex];
	}
	fformat(stdout, "}\n");
}


/*
 * KeeperFSMToJSONAppendEdge appends one {"current": ..., "assigned": ...}
 * object to array for a single KeeperFSMTransition row. current is rendered
 * as the literal string "any" for ANY_STATE (state_matches()'s wildcard,
 * e.g. fsm.c's "drop node from any state" rows) instead of
 * NodeStateToString(ANY_STATE)'s own "#any state#" -- see KeeperFSMToJSON's
 * own comment for why this sentinel exists and how the SQL side matches it.
 */
static void
KeeperFSMToJSONAppendEdge(JSON_Array *array, NodeState current, NodeState assigned)
{
	JSON_Value *jsEntry = json_value_init_object();
	JSON_Object *jsObj = json_value_get_object(jsEntry);

	json_object_set_string(jsObj, "current",
							current == ANY_STATE ? "any" : NodeStateToString(current));
	json_object_set_string(jsObj, "assigned", NodeStateToString(assigned));

	json_array_append_value(array, jsEntry);
}


/*
 * KeeperFSMToJSON serializes KeeperFSM[] into a JSON array of
 * {"current": ..., "assigned": ...} objects -- one per KeeperFSMTransition
 * row, walked the same way print_fsm_for_graphviz/print_reachable_states
 * already do. This is the keeper-side half of the monitor/keeper FSM
 * reachability cross-check: "pg_autoctl inspect fsm check" sends this
 * verbatim to the monitor's pgautofailover.check_fsm_reachability(jsonb),
 * which anti-joins it against every edge the monitor's own MonitorFSM[]
 * table (group_state_machine.c) can produce (see dump_fsm_edges()'s own
 * comment there) and reports any monitor transition with no matching entry
 * here.
 *
 * A row's own .current can be ANY_STATE -- rendered here as the literal
 * sentinel "any" (see KeeperFSMToJSONAppendEdge), not expanded into one
 * edge per concrete state: an earlier version of this function expanded it
 * against a hand-maintained list of "every real NodeState", which silently
 * under-covers the real ANY_STATE semantics the moment that list drifts out
 * of sync with the actual NodeState enum -- a false negative baked
 * permanently into the fixture, with nothing to ever catch it. Emitting the
 * wildcard literally instead lets the SQL comparison (check_fsm_reachability(),
 * and this project's own keeper_fsm_edges.sql regress test) match it against
 * whatever current_state values pgautofailover.dump_fsm_edges() actually
 * produces, so it can never drift out of sync with the real state universe.
 * .assigned is never ANY_STATE in any current KeeperFSM[] row (an
 * assignment target wildcard has no sensible meaning), so it's passed
 * through as-is.
 *
 * Deliberately omits pgKind and comment: pgKind is not modeled on the
 * monitor side of this check at all (every Citus-specific KeeperFSM[] edge
 * already has a NODE_KIND_ANY counterpart with the same (current, assigned)
 * shape -- see this same file's fsm_mermaid.c-referencing comment), and
 * comment is purely descriptive, never part of the comparison.
 *
 * Returns a malloc'd string (via json_serialize_to_string) the caller must
 * free with json_free_serialized_string().
 */
char *
KeeperFSMToJSON(void)
{
	JSON_Value *jsArray = json_value_init_array();
	JSON_Array *array = json_value_get_array(jsArray);

	KeeperFSMTransition transition = KeeperFSM[0];
	int transitionIndex = 0;

	while (transition.current != NO_STATE)
	{
		KeeperFSMToJSONAppendEdge(array, transition.current, transition.assigned);

		transition = KeeperFSM[++transitionIndex];
	}

	char *serialized = json_serialize_to_string(jsArray);

	json_value_free(jsArray);

	return serialized;
}
