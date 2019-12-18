/*
 * src/bin/pg_autoctl/state.h
 *     Keeper state data structure and function definitions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef STATE_H
#define STATE_H

#include "parson.h"

#include "keeper_config.h"
#include "pgctl.h"
#include "pgsetup.h"

/*
 * We need 80 bytes, but we'll mimic what PostgreSQL is doing with the handling
 * of its super important pg_control file, see the following files:
 *
 *   postgresql/src/include/catalog/pg_control.h
 *   postgresql/src/bin/pg_resetwal/pg_resetwal.c
 */
#define PG_AUTOCTL_KEEPER_STATE_FILE_SIZE 1024


/*
 * The keeper State Machine handle the following possible states:
 */
typedef enum
{
	NO_STATE = 0,
	INIT_STATE,
	SINGLE_STATE,
	PRIMARY_STATE,
	WAIT_PRIMARY_STATE,
	WAIT_STANDBY_STATE,
	DEMOTED_STATE,
	DEMOTE_TIMEOUT_STATE,
	DRAINING_STATE,
	SECONDARY_STATE,
	CATCHINGUP_STATE,
	PREP_PROMOTION_STATE,
	STOP_REPLICATION_STATE,
	MAINTENANCE_STATE,
	JOIN_PRIMARY_STATE,
	APPLY_SETTINGS_STATE,
	REPORT_LSN_STATE,
	WAIT_FORWARD_STATE,
	FAST_FORWARD_STATE,
	WAIT_CASCADE_STATE,

	/* Allow some wildcard-matching transitions (from ANY state to) */
	ANY_STATE = 128
} NodeState;

/*
 * ANY_STATE matches with any state, as its name implies:
 */
#define state_matches(x, y) (x == ANY_STATE || y == ANY_STATE || x == y)

/*
 * PostgreSQL prepared transaction names are up to 200 bytes.
 */
#define PREPARED_TRANSACTION_NAMELEN 200


/*
 * The Keeper's state is composed of information from three different sources:
 *  - the PostgreSQL instance we keep,
 *  - the pg_auto_failover Monitor, via the pgautofailover.node_active protocol,
 *  - the Keeper itself.
 *
 * The state is authoritative only for entries that are maintained by the
 * keeper itself, that's:
 *  - current_role
 *  - last_secondary_contact
 *  - xlog_location                    note: should we keep that?
 *  - keeper_is_paused
 */
typedef struct
{
	int pg_autoctl_state_version;

	/* PostgreSQL instance information, from pg_ctl and pg_controldata */
	int pg_version;
	uint32_t pg_control_version;    /* PG_CONTROL_VERSION */
	uint32_t catalog_version_no;    /* see catversion.h */
	uint64_t system_identifier;

	/* Information we get from the Monitor */
	int current_node_id;
	int current_group;
	NodeState assigned_role;
	uint64_t current_nodes_version;
	uint64_t last_monitor_contact;

	/* keeper's current state, authoritative */
	NodeState current_role;
	uint64_t last_secondary_contact;
	int64_t xlog_lag;
	int keeper_is_paused;
} KeeperStateData;


typedef enum
{
	PRE_INIT_STATE_UNKNOWN = 0,
	PRE_INIT_STATE_EMPTY,
	PRE_INIT_STATE_EXISTS,
	PRE_INIT_STATE_RUNNING,
	PRE_INIT_STATE_PRIMARY
} PreInitPostgreInstanceState;

typedef struct
{
	int pg_autoctl_state_version;
	PreInitPostgreInstanceState pgInitState;
} KeeperStateInit;

const char * NodeStateToString(NodeState s);
NodeState NodeStateFromString(const char *str);
const char * epoch_to_string(uint64_t seconds);

void keeper_state_init(KeeperStateData *keeperState);
bool keeper_state_create_file(const char *filename);
bool keeper_state_read(KeeperStateData *keeperState, const char *filename);
bool keeper_state_write(KeeperStateData *keeperState, const char *filename);

void log_keeper_state(KeeperStateData *keeperState);
void print_keeper_state(KeeperStateData *keeperState, FILE *fp);
bool keeperStateAsJSON(KeeperStateData *keeperState, JSON_Value *js);
void print_keeper_init_state(KeeperStateInit *initState, FILE *stream);

char *PreInitPostgreInstanceStateToString(PreInitPostgreInstanceState pgInitState);

#endif /* STATE_H */
