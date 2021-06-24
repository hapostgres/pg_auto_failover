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

#include <assert.h>
#include "parson.h"

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
	PREPARE_MAINTENANCE_STATE,
	WAIT_MAINTENANCE_STATE,
	REPORT_LSN_STATE,
	FAST_FORWARD_STATE,
	JOIN_SECONDARY_STATE,
	DROPPED_STATE,

	/* Allow some wildcard-matching transitions (from ANY state to) */
	ANY_STATE = 128
} NodeState;

#define MAX_NODE_STATE_LEN 19   /* "prepare_maintenance" */

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
 *
 *  Note: The struct is serialized/serialiazed to/from state file. Therefore
 *  keeping the memory layout the same is important. Please
 *  - do not change the order of fields
 *  - do not add a new field in between, always add to the end
 *  - do not use any pointers
 *
 * The nodeId used to be a 32 bits integer on the monitor, and has been
 * upgraded to a bigint (64 bits). That said, the on-disk state file still
 * works internally with a 32 bits number for the nodeId.
 *
 * When that's needed, we could create a compatibility function that knows how
 * to read the old state with an int32_t and then fill-in the new struct with a
 * 664 bits number instead, and serialize that to disk transparently. As it is
 * not expected to find nodeId in the wild, this work has not been done yet.
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

_Static_assert(sizeof(KeeperStateData) < PG_AUTOCTL_KEEPER_STATE_FILE_SIZE,
			   "Size of KeeperStateData is larger than expected. "
			   "Please review PG_AUTOCTL_KEEPER_STATE_FILE_SIZE");

/*
 * The init file contains the status of the target Postgres instance when the
 * pg_autoctl create command ran the first time. We need to be able to make
 * init time decision again if we're interrupted half-way and later want to
 * proceed. The instruction for the user to proceed in that case is to run the
 * pg_autoctl create command again.
 *
 * We also update the init file with the current stage of the initialisation
 * process. This allows communication to happen between the init process and
 * the Postgres FSM supervisor process. The Postgres FSM supervisors knows it
 * must start Postgres when reaching init stage 2.
 */
typedef enum
{
	PRE_INIT_STATE_UNKNOWN = 0,
	PRE_INIT_STATE_EMPTY,
	PRE_INIT_STATE_EXISTS,
	PRE_INIT_STATE_RUNNING,
	PRE_INIT_STATE_PRIMARY
} PreInitPostgreInstanceState;

/*
 *  Note: The struct is serialized/serialiazed to/from state file. Therefore
 *  keeping the memory layout the same is important. Please
 *  - do not change the order of fields
 *  - do not add a new field in between, always add to the end
 *  - do not use any pointers
 */
typedef struct
{
	int pg_autoctl_state_version;
	PreInitPostgreInstanceState pgInitState;
} KeeperStateInit;

_Static_assert(sizeof(KeeperStateInit) < PG_AUTOCTL_KEEPER_STATE_FILE_SIZE,
			   "Size of KeeperStateInit is larger than expected. "
			   "Please review PG_AUTOCTL_KEEPER_STATE_FILE_SIZE");


/*
 * pg_autoctl manages Postgres as a child process. The FSM loop runs in the
 * node-active sub-process, and that's where decisions are made depending on
 * the current state and transition whether Postgres should be running or not.
 *
 * The communication between the node-active process and the Postgres
 * start/stop controller process is done by means of the Postgres state file,
 * which is basically a boolean. That said, we want to make sure we read the
 * file content correctly, so 0 is unknown.
 */
typedef enum
{
	PG_EXPECTED_STATUS_UNKNOWN = 0,
	PG_EXPECTED_STATUS_STOPPED,
	PG_EXPECTED_STATUS_RUNNING,
	PG_EXPECTED_STATUS_RUNNING_AS_SUBPROCESS
} ExpectedPostgresStatus;

/*
 *  Note: This struct is serialized/deserialized to/from state file. Therefore
 *  keeping the memory layout the same is important. Please
 *  - do not change the order of fields
 *  - do not add a new field in between, always add to the end
 *  - do not use any pointers
 */
typedef struct
{
	int pg_autoctl_state_version;
	ExpectedPostgresStatus pgExpectedStatus;
} KeeperStatePostgres;

_Static_assert(sizeof(KeeperStatePostgres) < PG_AUTOCTL_KEEPER_STATE_FILE_SIZE,
			   "Size of KeeperStatePostgres is larger than expected. "
			   "Please review PG_AUTOCTL_KEEPER_STATE_FILE_SIZE");

const char * NodeStateToString(NodeState s);
NodeState NodeStateFromString(const char *str);
const char * epoch_to_string(uint64_t seconds, char *buffer);

void keeper_state_init(KeeperStateData *keeperState);
bool keeper_state_create_file(const char *filename);
bool keeper_state_read(KeeperStateData *keeperState, const char *filename);
bool keeper_state_write(KeeperStateData *keeperState, const char *filename);

void log_keeper_state(KeeperStateData *keeperState);
void print_keeper_state(KeeperStateData *keeperState, FILE *fp);
bool keeperStateAsJSON(KeeperStateData *keeperState, JSON_Value *js);

void print_keeper_init_state(KeeperStateInit *initState, FILE *stream);
char * PreInitPostgreInstanceStateToString(PreInitPostgreInstanceState pgInitState);

bool keeper_init_state_create(KeeperStateInit *initState,
							  PostgresSetup *pgSetup,
							  const char *filename);
bool keeper_init_state_read(KeeperStateInit *initState, const char *filename);
bool keeper_init_state_discover(KeeperStateInit *initState,
								PostgresSetup *pgSetup,
								const char *filename);

char * ExpectedPostgresStatusToString(ExpectedPostgresStatus pgExpectedStatus);

bool keeper_set_postgres_state_unknown(KeeperStatePostgres *pgStatus,
									   const char *filename);
bool keeper_set_postgres_state_running(KeeperStatePostgres *pgStatus,
									   const char *filename);
bool keeper_set_postgres_state_running_as_subprocess(KeeperStatePostgres *pgStatus,
													 const char *filename);
bool keeper_set_postgres_state_stopped(KeeperStatePostgres *pgStatus,
									   const char *filename);
bool keeper_postgres_state_update(KeeperStatePostgres *pgStatus,
								  const char *filename);
bool keeper_postgres_state_read(KeeperStatePostgres *pgStatus,
								const char *filename);


#endif /* STATE_H */
