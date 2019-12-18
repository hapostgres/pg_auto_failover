/*
 * src/bin/pg_autoctl/state.c
 *     Keeper state functions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"
#include "libpq-fe.h"

#include "parson.h"

#include "defaults.h"
#include "file_utils.h"
#include "keeper_config.h"
#include "keeper.h"
#include "log.h"
#include "pgctl.h"
#include "pgsetup.h"
#include "pgsql.h"
#include "state.h"

static bool keeper_state_is_readable(int pg_autoctl_state_version);


/*
 * keeper_state_read initializes our current state in-memory from disk.
 */
bool
keeper_state_read(KeeperStateData *keeperState, const char *filename)
{
	char *content = NULL;
	long fileSize;
	int pg_autoctl_state_version = 0;

	log_debug("Reading current state from \"%s\"", filename);

	if (!read_file(filename, &content, &fileSize))
	{
		log_error("Failed to read Keeper state from file \"%s\"", filename);
		return false;
	}

	pg_autoctl_state_version =
		((KeeperStateData *) content)->pg_autoctl_state_version;

	if (fileSize >= sizeof(KeeperStateData)
		&& keeper_state_is_readable(pg_autoctl_state_version))
	{
		memcpy(keeperState, content, sizeof(KeeperStateData));
		free(content);
		return true;
	}

	free(content);

	/* Looks like it's a mess. */
	log_error("Keeper state file \"%s\" exists but is broken or wrong version",
			  filename);
	return false;
}


/*
 * keeper_state_is_readable returns true if we can read a state file from the
 * given version of pg_autoctl.
 */
static bool
keeper_state_is_readable(int pg_autoctl_state_version)
{
	return pg_autoctl_state_version == PG_AUTOCTL_STATE_VERSION
		|| (pg_autoctl_state_version == 1
			&& PG_AUTOCTL_STATE_VERSION == 2);
}

/*
 * The KeeperState data structure contains only direct values (int, long), not
 * a single pointer, so writing to disk is a single fwrite() instruction.
 *
 */
bool
keeper_state_write(KeeperStateData *keeperState, const char *filename)
{
	int fd;
	char buffer[PG_AUTOCTL_KEEPER_STATE_FILE_SIZE];
	char tempFileName[MAXPGPATH];

	/* we're going to write our contents to keeper.state.new first */
	sprintf(tempFileName, "%s.new", filename);

	/*
	 * The keeper process might have been stopped in immediate shutdown mode
	 * (SIGQUIT) and left a stale state.new file around, or maybe another
	 * situation led to a file at tempFileName existing already. Clean-up the
	 * stage before preparing our new state file's content.
	 */
	if (!unlink_file(tempFileName))
	{
		/* errors have already been logged */
		return false;
	}

	log_debug("Writing current state to \"%s\"", tempFileName);

	/*
	 * Comment kept as is from PostgreSQL source code, function
	 * RewriteControlFile() in postgresql/src/bin/pg_resetwal/pg_resetwal.c
	 *
	 * We write out PG_CONTROL_FILE_SIZE bytes into pg_control, zero-padding
	 * the excess over sizeof(ControlFileData).  This reduces the odds of
	 * premature-EOF errors when reading pg_control.  We'll still fail when we
	 * check the contents of the file, but hopefully with a more specific
	 * error than "couldn't read pg_control".
	 */
	memset(buffer, 0, PG_AUTOCTL_KEEPER_STATE_FILE_SIZE);
	memcpy(buffer, keeperState, sizeof(KeeperStateData));

	fd = open(tempFileName, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	if (fd < 0)
	{
		log_fatal("Failed to create keeper state file \"%s\": %s",
				  tempFileName, strerror(errno));
		return false;
	}

	errno = 0;
	if (write(fd, buffer, PG_AUTOCTL_KEEPER_STATE_FILE_SIZE) !=
		PG_AUTOCTL_KEEPER_STATE_FILE_SIZE)
	{
		/* if write didn't set errno, assume problem is no disk space */
		if (errno == 0)
		{
			errno = ENOSPC;
		}
		log_fatal("Failed to write keeper state file \"%s\": %s",
				  tempFileName, strerror(errno));
		return false;
	}

	if (fsync(fd) != 0)
	{
		log_fatal("fsync error: %s", strerror(errno));
		return false;
	}

	close(fd);

	log_debug("rename \"%s\" to \"%s\"", tempFileName, filename);

	/* now remove the old state file, and replace it with the new one */
	if (rename(tempFileName, filename) != 0)
	{
		log_fatal("Failed to rename \"%s\" to \"%s\": %s",
				  tempFileName, filename, strerror(errno));
		return false;
	}

	return true;
}


/*
 * keeper_state_init initializes a new state structure with default values.
 */
void
keeper_state_init(KeeperStateData *keeperState)
{
	memset(keeperState, 0, sizeof(KeeperStateData));

	keeperState->pg_autoctl_state_version = PG_AUTOCTL_STATE_VERSION;
	keeperState->current_node_id = -1;
	keeperState->current_group = 1;

	/* a node always starts in the init state and transitions from there */
	keeperState->current_role = INIT_STATE;

	/* we do not know our assigned state yet */
	keeperState->assigned_role = NO_STATE;

	/* we do not know the xlog lag of the secondary */
	keeperState->xlog_lag = -1;
}


/*
 * keeper_state_create_file creates an initial state file from the given
 * postgres setup and group ID.
 */
bool
keeper_state_create_file(const char *filename)
{
	KeeperStateData keeperState;

	keeper_state_init(&keeperState);

	return keeper_state_write(&keeperState, filename);
}


/*
 * log_keeper_state dumps the current in memory state to the logs.
 */
void
log_keeper_state(KeeperStateData *keeperState)
{
	const char *current_role = NodeStateToString(keeperState->current_role);
	const char *assigned_role = NodeStateToString(keeperState->assigned_role);

	log_trace("state.pg_control_version: %u", keeperState->pg_control_version);
	log_trace("state.system_identifier: %" PRIu64, keeperState->system_identifier);
	log_trace("state.pg_autoctl_state_version: %d", keeperState->pg_autoctl_state_version);
	log_trace("state.current_node_id: %d", keeperState->current_node_id);
	log_trace("state.current_group: %d", keeperState->current_group);
	log_trace("state.current_nodes_version: %" PRIu64,
			  keeperState->current_nodes_version);

	log_trace("state.current_role: %s", current_role);
	log_trace("state.assigned_role: %s", assigned_role);

	log_trace("state.last_monitor_contact: %s",
			  epoch_to_string(keeperState->last_monitor_contact));

	log_trace("state.last_secondary_contact: %s",
			  epoch_to_string(keeperState->last_secondary_contact));

	log_trace("state.xlog_lag : %" PRId64, keeperState->xlog_lag);

	log_trace("state.keeper_is_paused: %d", keeperState->keeper_is_paused);
	log_trace("state.pg_version: %d", keeperState->pg_version);
}


/*
 * print_keeper_state prints the current in-memory state of the keeper to given
 * FILE output (stdout, stderr, etc).
 */
void
print_keeper_state(KeeperStateData *keeperState, FILE *stream)
{
	const char *current_role = NodeStateToString(keeperState->current_role);
	const char *assigned_role = NodeStateToString(keeperState->assigned_role);

	/*
	 * First, the roles.
	 */
	fprintf(stream, "Current Role:             %s\n", current_role);
	fprintf(stream, "Assigned Role:            %s\n", assigned_role);

	/*
	 * Now, other nodes situation, are we in a network partition.
	 */
	fprintf(stream, "Last Monitor Contact:     %s\n",
			epoch_to_string(keeperState->last_monitor_contact));

	fprintf(stream, "Last Secondary Contact:   %s\n",
			epoch_to_string(keeperState->last_secondary_contact));

	/*
	 * pg_autoctl information.
	 */
	fprintf(stream, "pg_autoctl state version: %d\n",
			keeperState->pg_autoctl_state_version);
	fprintf(stream, "group:                    %d\n", keeperState->current_group);
	fprintf(stream, "node id:                  %d\n", keeperState->current_node_id);
	fprintf(stream, "nodes version:            %" PRIu64 "\n",
			keeperState->current_nodes_version);

	/*
	 * PostgreSQL bits.
	 */
	fprintf(stream, "PostgreSQL Version:       %u\n",
			keeperState->pg_control_version);
	fprintf(stream, "PostgreSQL CatVersion:    %u\n",
			keeperState->catalog_version_no);
	fprintf(stream, "PostgreSQL System Id:     %" PRIu64 "\n",
			keeperState->system_identifier);

	fflush(stream);
}


/*
 * keeperStateAsJSON
 */
bool
keeperStateAsJSON(KeeperStateData *keeperState, JSON_Value *js)
{
	JSON_Object *jsobj = json_value_get_object(js);
	const char *current_role = NodeStateToString(keeperState->current_role);
	const char *assigned_role = NodeStateToString(keeperState->assigned_role);

	json_object_set_string(jsobj, "current_role", current_role);
	json_object_set_string(jsobj, "assigned_role", assigned_role);

	json_object_set_number(jsobj, "version",
						   (double) keeperState->pg_autoctl_state_version);

	json_object_set_number(jsobj, "groupId",
						   (double) keeperState->current_group);

	json_object_set_number(jsobj, "nodeId",
						   (double) keeperState->current_node_id);

	return true;
}

/*
 * print_keeper_init_state prints the given initilization state of the keeper
 * to given FILE output (stdout, stderr, etc).
 */
void
print_keeper_init_state(KeeperStateInit *initState, FILE *stream)
{
	fprintf(stream, "Postgres state at keeper init: %s\n",
			PreInitPostgreInstanceStateToString(initState->pgInitState));
	fflush(stream);
}

/*
 * NodeStateToString converts a NodeState ENUM value into a string for use in
 * user reporting.
 */
const char *
NodeStateToString(NodeState s)
{
	switch (s)
	{
		case NO_STATE:
		{
			return "unknown";
		}

		case INIT_STATE:
		{
			return "init";
		}

		case SINGLE_STATE:
		{
			return "single";
		}

		case PRIMARY_STATE:
		{
			return "primary";
		}

		case WAIT_PRIMARY_STATE:
		{
			return "wait_primary";
		}

		case WAIT_STANDBY_STATE:
		{
			return "wait_standby";
		}

		case DEMOTED_STATE:
		{
			return "demoted";
		}

		case DEMOTE_TIMEOUT_STATE:
		{
			return "demote_timeout";
		}

		case DRAINING_STATE:
		{
			return "draining";
		}

		case SECONDARY_STATE:
		{
			return "secondary";
		}

		case CATCHINGUP_STATE:
		{
			return "catchingup";
		}

		case PREP_PROMOTION_STATE:
		{
			return "prepare_promotion";
		}

		case STOP_REPLICATION_STATE:
		{
			return "stop_replication";
		}

		case MAINTENANCE_STATE:
		{
			return "maintenance";
		}

		case JOIN_PRIMARY_STATE:
		{
			return "join_primary";
		}

		case APPLY_SETTINGS_STATE:
		{
			return "apply_settings";
		}

		case REPORT_LSN_STATE:
		{
			return "report_lsn";
		}

		case FAST_FORWARD_STATE:
		{
			return "fast_forward";
		}

		case WAIT_FORWARD_STATE:
		{
			return "wait_forward";
		}

		case WAIT_CASCADE_STATE:
		{
			return "wait_cascade";
		}

		case ANY_STATE:
		{
			return "#any state#";
		}

		default:
			return "Unknown State";
	}
	return NULL;
}


/*
 * NodeStateFromString converts a string representation of a node state into
 * the corresponding internal ENUM value.
 */
NodeState
NodeStateFromString(const char *str)
{
	if (strcmp(str, "unknown") == 0)
	{
		return NO_STATE;
	}
	else if (strcmp(str, "init") == 0)
	{
		return INIT_STATE;
	}
	else if (strcmp(str, "single") == 0)
	{
		return SINGLE_STATE;
	}
	else if (strcmp(str, "primary") == 0)
	{
		return PRIMARY_STATE;
	}
	else if (strcmp(str, "wait_primary") == 0)
	{
		return WAIT_PRIMARY_STATE;
	}
	else if (strcmp(str, "wait_standby") == 0)
	{
		return WAIT_STANDBY_STATE;
	}
	else if (strcmp(str, "demoted") == 0)
	{
		return DEMOTED_STATE;
	}
	else if (strcmp(str, "demote_timeout") == 0)
	{
		return DEMOTE_TIMEOUT_STATE;
	}
	else if (strcmp(str, "draining") == 0)
	{
		return DRAINING_STATE;
	}
	else if (strcmp(str, "secondary") == 0)
	{
		return SECONDARY_STATE;
	}
	else if (strcmp(str, "catchingup") == 0)
	{
		return CATCHINGUP_STATE;
	}
	else if (strcmp(str, "prepare_promotion") == 0)
	{
		return PREP_PROMOTION_STATE;
	}
	else if (strcmp(str, "stop_replication") == 0)
	{
		return STOP_REPLICATION_STATE;
	}
	else if (strcmp(str, "maintenance") == 0)
	{
		return MAINTENANCE_STATE;
	}
	else if (strcmp(str, "join_primary") == 0)
	{
		return JOIN_PRIMARY_STATE;
	}
	else if(strcmp(str, "apply_settings") == 0)
	{
		return APPLY_SETTINGS_STATE;
	}
	else if(strcmp(str, "report_lsn") == 0)
	{
		return REPORT_LSN_STATE;
	}
	else if(strcmp(str, "fast_forward") == 0)
	{
		return FAST_FORWARD_STATE;
	}
	else if(strcmp(str, "wait_forward") == 0)
	{
		return WAIT_FORWARD_STATE;
	}
	else if(strcmp(str, "wait_cascade") == 0)
	{
		return WAIT_CASCADE_STATE;
	}
	else
	{
		log_fatal("Failed to parse state string \"%s\"", str);
		return NO_STATE;
	}
	return NO_STATE;
}


/*
 * epoch_to_string converts a number of seconds from epoch into a date time
 * string.
 */
const char *
epoch_to_string(uint64_t seconds)
{
	if (seconds > 0)
	{
		/*
		 * ctime() returns a string that ends with \n, which we don't want.
		 * Replace it with a null string terminator, even if that means we lose
		 * a precious byte of memory here..
		 */
		char *str = ctime((time_t *) &seconds);

		if (str == NULL)
		{
			log_error("Failed to convert epoch %" PRIu64 " to string: %s",
					  seconds, strerror(errno));
			return NULL;
		}
		str[strlen(str) - 1] = '\0';
		return str;
	}
	return "0";
}

/*
 * PreInitPostgreInstanceStateToString returns the string that represents the
 * init state of the local PostgreSQL instance.
 */
char *
PreInitPostgreInstanceStateToString(PreInitPostgreInstanceState pgInitState)
{
	switch (pgInitState)
	{
		case PRE_INIT_STATE_EMPTY:
			return "PGDATA does not exists";

		case PRE_INIT_STATE_EXISTS:
			return "PGDATA exists";

		case PRE_INIT_STATE_RUNNING:
			return "PostgreSQL is running";

		case PRE_INIT_STATE_PRIMARY:
			return "PostgreSQL is running and a primary server";

		default:
			return "unknown";
	}

	/* keep compiler happy */
	return "unknown";
}
