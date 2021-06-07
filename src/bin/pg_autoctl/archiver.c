/*
 * src/bin/pg_autoctl/archiver.c
 *	 API for interacting with the archiver
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <inttypes.h>
#include <limits.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "archiver.h"
#include "archiver_config.h"
#include "defaults.h"
#include "env_utils.h"
#include "log.h"
#include "parsing.h"
#include "string_utils.h"

/*
 * Process Tree:
 *
 * pg_autoctl run (archiver)
 *   pg_autoctl do service archiver-node formation groupid
 *     pg_autoctl do service node-active
 *     pg_autoctl do service archiver-schedule formation groupid
 *       pg_autoctl archive create backup
 *       pg_autoctl archive prune
 *     pg_autoctl do service postgres
 *   ...
 *   pg_autoctl do service archiver-node formation groupid
 *   ...
 *
 * archive_command = 'pg_autoctl archive wal %p'
 *
 * Directories: (make it easy to rsync/rclone etc)
 *
 *  topdir = /var/lib/postgresql/archives
 *
 *   PGDATA   topdir/node/${formation}/${groupid}
 *   PG_WAL   topdir/pg_wal/${formation}/${groupid}
 *   BACKUP   topdir/backup/${formation}/${groupid}
 */

/*
 * archiver_monitor_init initialises a connection to the monitor.
 */
bool
archiver_monitor_init(Archiver *archiver)
{
	if (!monitor_init(&(archiver->monitor), archiver->config.monitor_pguri))
	{
		/* errors have already been logged */
		return false;
	}

	return true;
}


/*
 * keeper_register_and_init registers the local node to the pg_auto_failover
 * Monitor, and then create the state on-disk with the assigned goal from the
 * Monitor.
 */
bool
archiver_register_and_init(Archiver *archiver)
{
	ArchiverConfig *config = &(archiver->config);

	Monitor *monitor = &(archiver->monitor);
	ConnectionRetryPolicy retryPolicy = { 0 };

	NodeAddress node = { 0 };

	/*
	 * First try to create our state file. The archiver_state_create_file
	 * function may fail if we have no permission to write to the state file
	 * directory or the disk is full. In that case, we stop before having
	 * registered the archiver to the monitor.
	 */
	if (!archiver_state_create_file(config->pathnames.state))
	{
		log_fatal("Failed to create a state file prior to registering the "
				  "node with the monitor, see above for details");
		return false;
	}

	/*
	 * Now, initialise our monitor instance, to connect and register there.
	 */
	if (!archiver_monitor_init(archiver))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	/* Use our monitor interactive retry policy for registration. */
	(void) pgsql_set_monitor_interactive_retry_policy(&retryPolicy);

	/*
	 * We register to the monitor in a SQL transaction that we only COMMIT
	 * after we have updated our local state file. If we fail to do so, we
	 * ROLLBACK the transaction, and thus we are not registered to the
	 * monitor and may try again. If we are disconnected halfway through
	 * the registration (process killed, crash, etc), then the server
	 * issues a ROLLBACK for us upon disconnection.
	 */
	if (!pgsql_begin(&(monitor->pgsql)))
	{
		log_error("Failed to open a SQL transaction to register this node");

		unlink_file(config->pathnames.state);
		return false;
	}

	/* now register on the monitor */
	if (!monitor_register_archiver(monitor,
								   config->name,
								   config->hostname,
								   &node))
	{
		/* errors have already been logged */
		goto rollback;
	}

	if (!archiver_update_state(archiver, node.nodeId))
	{
		log_error("Failed to update archiver state");

		goto rollback;
	}

	if (!pgsql_commit(&(monitor->pgsql)))
	{
		log_error("Failed to COMMIT register_archiver transaction on the "
				  "monitor, see above for details");

		/* we can't send a ROLLBACK when a COMMIT failed */
		unlink_file(config->pathnames.state);

		pgsql_finish(&(monitor->pgsql));
		return false;
	}

	pgsql_finish(&(monitor->pgsql));
	return true;

rollback:

	/*
	 * Make sure we don't have a corrupted state file around, that could
	 * prevent trying to init again and cause strange errors.
	 */
	unlink_file(config->pathnames.state);

	if (!pgsql_rollback(&(monitor->pgsql)))
	{
		log_error("Failed to ROLLBACK failed register_node transaction "
				  " on the monitor, see above for details.");
	}
	pgsql_finish(&(monitor->pgsql));

	return false;
}


/*
 * archiver_node_register_and_init registers a archive (standby) node for the
 * monitor. Every instance of an archiver is automatically activated for the
 * monitor itself, so that we have copies around.
 */
bool
archiver_node_register_and_init(Archiver *archiver,
								char *formation,
								int groupId,
								char *dbname,
								int pgport,
								PgInstanceKind kind,
								bool replicationQuorum)
{
	Monitor *monitor = &(archiver->monitor);

	bool mayRetry = false;
	MonitorAssignedState assignedState = { 0 };

	if (!monitor_register_archiver_node(monitor,
										archiver->state.archiverId,
										formation,
										"", /* the monitor assigns a name */
										archiver->config.hostname,
										pgport,
										0, /* we don't have a sysIdentifier */
										dbname,
										-1, /* desiredNodeId */
										groupId,
										INIT_STATE,
										kind,
										replicationQuorum,
										&mayRetry,
										&assignedState))
	{
		log_error("Failed to register an archiver node for archiver %d "
				  "in formation \"%s\" and group %d, see above for details.",
				  archiver->state.archiverId, formation, groupId);
		return false;
	}

	return true;
}
