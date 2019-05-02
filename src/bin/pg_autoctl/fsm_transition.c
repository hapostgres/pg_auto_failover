/*
 * src/bin/pg_autoctl/fsm_transition.c
 *   Implementation of transitions in the keeper state machine
 *
 * To move from a current state to a goal state, the pg_autoctl state machine
 * will call the functions defined in this file, which are referenced from
 * fsm.c
 *
 * Every transition must be idempotent such that it can safely be repeated
 * until it succeeds.
 *
 * As the keeper could fail or be interrupted in-flight, it's important that
 * every transition can be tried again (is idempotent). When interrupted (by
 * a bug or a signal, user interrupt or system reboot), the current and
 * assigned roles have not changed and on the next keeper's start the FSM
 * will kick in a call the transition that failed again. The transition might
 * have successfully implemented the first parts of its duties... and we must
 * not fail because of that. Idempotency is achieved by only calling
 * idempotent subroutines or checking whether the goal of the subroutine
 * (e.g. "postgres is promoted") has been achieved already.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <time.h>
#include <unistd.h>

#include "coordinator.h"
#include "defaults.h"
#include "pgctl.h"
#include "fsm.h"
#include "keeper.h"
#include "log.h"
#include "monitor.h"
#include "primary_standby.h"
#include "state.h"

static bool prepare_replication(Keeper *keeper, bool other_node_missing_is_ok);


/*
 * fsm_init_primary initialises the postgres server as primary.
 *
 *    start_postgres
 * && promote_standby (if applicable)
 * && add_default_settings
 * && create_monitor_user
 * && create_replication_user
 */
bool
fsm_init_primary(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	LocalPostgresServer *postgres = &(keeper->postgres);
	PGSQL *pgsql = &(postgres->sqlClient);
	bool inRecovery = false;
	char *password = NULL;
	char monitorHostname[_POSIX_HOST_NAME_MAX];

	log_info("Initialising postgres as a primary");

	if (!hostname_from_uri(config->monitor_pguri, monitorHostname, _POSIX_HOST_NAME_MAX))
	{
		/* developer error, this should never happen */
		log_fatal("BUG: monitor_pguri should be validated before calling "
				  "fsm_init_primary");
		return false;
	}

	if (!ensure_local_postgres_is_running(postgres))
	{
		log_error("Failed to initialise postgres as primary because starting postgres "
				  "failed, see above for details");
		return false;
	}

	if (pgsql_is_in_recovery(pgsql, &inRecovery) && inRecovery)
	{
		log_info("Initialising a postgres server in recovery mode as the primary, "
				 "promoting");

		if (!standby_promote(postgres))
		{
			log_error("Failed to initialise postgres as primary because promoting "
					  "postgres failed, see above for details");
			return false;
		}
	}

	if (!postgres_add_default_settings(postgres))
	{
		log_error("Failed to initialise postgres as primary because adding default "
				  "settings failed, see above for details");
		return false;
	}

	password = NULL;

	if (!primary_create_user_with_hba(postgres,
									  PG_AUTOCTL_HEALTH_USERNAME, password,
									  monitorHostname))
	{
		log_error("Failed to initialise postgres as primary because creating the "
				  "database user that the pg_auto_failover monitor "
				  "uses for health checks failed, see above for details");
		return false;
	}

	if (!primary_create_replication_user(postgres, PG_AUTOCTL_REPLICA_USERNAME,
										 config->replication_password))
	{
		log_error("Failed to initialise postgres as primary because creating the "
				  "replication user for the standby failed, see above for details");
		return false;
	}

	/* and we're done with this connection. */
	pgsql_finish(pgsql);

	return true;
}


/*
 * fsm_disable_replication is used when other node was forcibly removed, now
 * single.
 *
 * disable_synchronous_replication && drop_replication_slot
 *
 * TODO: We currently use a separate session for each step. We should use
 * a single connection.
 */
bool
fsm_disable_replication(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	LocalPostgresServer *postgres = &(keeper->postgres);

	if (!primary_disable_synchronous_replication(postgres))
	{
		log_error("Failed to disable replication because disabling synchronous "
				  "failed, see above for details");
		return false;
	}

	if (!primary_drop_replication_slot(postgres, config->replication_slot_name))
	{
		log_error("Failed to disable replication because dropping the replication "
				  "slot \"%s\" used by the standby failed, see above for details",
				  config->replication_slot_name);
		return false;
	}

	return true;
}


/*
 * fsm_resume_as_primary is used when the local node was demoted after a
 * failure, but standby was forcibly removed.
 *
 *    start_postgres
 * && disable_synchronous_replication
 * && drop_replication_slot
 *
 * So we reuse fsm_disable_replication() here, rather than copy/pasting the same
 * bits code in the fsm_resume_as_primary() function body. If the definition of
 * the fsm_resume_as_primary transition ever came to diverge from whatever
 * fsm_disable_replication() is doing, we'd have to copy/paste and maintain
 * separate code path.
 */
bool
fsm_resume_as_primary(Keeper *keeper)
{
	if (!keeper_start_postgres(keeper))
	{
		return false;
	}

	if (!fsm_disable_replication(keeper))
	{
		log_error("Failed to disable synchronous replication in order to "
				  "resume as a primary, see above for details");
		return false;
	}

	return true;
}


/*
 * fsm_prepare_replication is used when a new standby was added.
 *
 * add_standby_to_hba && create_replication_slot
 */
bool
fsm_prepare_replication(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	bool other_node_missing_is_ok = false;

	if (IS_EMPTY_STRING_BUFFER(config->nodename))
	{
		/* developer error, this should never happen */
		log_fatal(
			"BUG: nodename should be set before calling fsm_prepare_replication");
		return false;
	}

	return prepare_replication(keeper, other_node_missing_is_ok);
}


/*
 * prepare_replication is the work-horse for fsm_prepare_replication and used
 * in fsm_promote_standby too, where we could have to accept the fact that
 * there's no other node at the moment: we're doing a secondary ➜ single
 * transition after all.
 */
static bool
prepare_replication(Keeper *keeper, bool other_node_missing_is_ok)
{
	KeeperConfig *config = &(keeper->config);
	Monitor *monitor = &(keeper->monitor);
	LocalPostgresServer *postgres = &(keeper->postgres);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);
	NodeAddress otherNode = { 0 };

	if (!monitor_get_other_node(monitor, config->nodename, pgSetup->pgport,
								&otherNode))
	{
		if (other_node_missing_is_ok)
		{
			log_debug("There's no other node for %s:%d",
					  config->nodename, pgSetup->pgport);
		}
		else
		{
			log_error("There's no other node for %s:%d",
					  config->nodename, pgSetup->pgport);
		}
		return other_node_missing_is_ok;
	}

	if (!primary_add_standby_to_hba(postgres,
									otherNode.host,
									config->replication_password))
	{
		log_error(
			"Failed to grant access to the standby by adding relevant lines to "
			"pg_hba.conf for the standby hostname and user, see above for "
			"details");
		return false;
	}

	if (!primary_create_replication_slot(postgres, config->replication_slot_name))
	{
		log_error(
			"Failed to enable replication from the primary server because "
			"creating the replication slot failed, see above for details");
		return false;
	}

	return true;
}


/*
 * fsm_stop_replication is used to forcefully stop replication, in case the
 * primary is on the other side of a network split.
 */
bool
fsm_stop_replication(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);
	PGSQL *client = &(postgres->sqlClient);

	/*
	 * We can't control if the client is still sending writes to our PostgreSQL
	 * instance or not. To avoid split-brains situation, we need to make some
	 * efforts:
	 *
	 * - set default_transaction_read_only to 'on' on this server (a
	 *   standby being promoted) so that it can't be the target of
	 *   connection strings requiring target_session_attrs=read-write yet
	 *
	 * - shut down the replication stream (here by promoting the replica)
	 *
	 * - have the primary server realize it's alone on the network: can't
	 *   communicate with the monitor (which triggered the failover), can't
	 *   communicate with the standby (now absent from pg_stat_replication)
	 *
	 * When the keeper on the primary realizes they are alone in the dark,
	 * it will go to DEMOTE state on its own and shut down PostgreSQL,
	 * protecting againts split brain.
	 */

	log_info("Prevent writes to the promoted standby while the primary "
			 "is not demoted yet, by making the service incompatible with "
			 "target_session_attrs = read-write");

	if (!pgsql_set_default_transaction_mode_read_only(client))
	{
		log_error("Failed to switch to read-only mode");
		return false;
	}

	return fsm_promote_standby(keeper);
}


/*
 * fsm_disable_sync_rep is used when standby became unhealthy.
 */
bool
fsm_disable_sync_rep(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);

	return primary_disable_synchronous_replication(postgres);
}


/*
 * fsm_promote_standby_to_primary is used when the standby should become the
 * new primary. It also prepares for the old primary to become the new standby.
 *
 * The promotion of the standby has already happened in the previous
 * transition:
 *
 *  1.         secondary ➜ prepare_promotion : block writes
 *  2. prepare_promotion ➜ stop_replication  : promote
 *  3.  stop_replication ➜ wait_primary      : resume writes
 *
 * Resuming writes is done by setting default_transaction_read_only to off,
 * thus allowing libpq to establish connections when target_session_attrs is
 * read-write.
 */
bool
fsm_promote_standby_to_primary(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);
	PGSQL *client = &(postgres->sqlClient);

	if (!pgsql_set_default_transaction_mode_read_write(client))
	{
		log_error("Failed to set default_transaction_read_only to off "
				  "which is needed to accept libpq connections with "
				  "target_session_attrs read-write");
		return false;
	}
	return true;
}


/*
 * fsm_enable_sync_rep is used when a healthy standby appeared.
 */
bool
fsm_enable_sync_rep(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);

	return primary_enable_synchronous_replication(postgres);
}


/*
 * fsm_start_postgres is used when we detected a network partition, but monitor
 * didn't do failover.
 */
bool
fsm_start_postgres(Keeper *keeper)
{
	return keeper_start_postgres(keeper);
}


/*
 * fsm_stop_postgres is used when local node was demoted, need to be dead now.
 */
bool
fsm_stop_postgres(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	PostgresSetup *pgSetup = &(config->pgSetup);

	return pg_ctl_stop(pgSetup->pg_ctl, pgSetup->pgdata);
}


/*
 * fsm_init_standby is used when the primary is now ready to accept a standby,
 * we're the standby.
 */
bool
fsm_init_standby(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	Monitor *monitor = &(keeper->monitor);
	LocalPostgresServer *postgres = &(keeper->postgres);
	ReplicationSource replicationSource = { 0 };
	int groupId = keeper->state.current_group;

	/* get the primary node to follow */
	if (!monitor_get_primary(monitor, config->formation, groupId,
							 &replicationSource.primaryNode))
	{
		log_error("Failed to initialise standby because get the primary node "
				  "from the monitor failed, see above for details");
		return false;
	}

	replicationSource.userName = PG_AUTOCTL_REPLICA_USERNAME;
	replicationSource.password = config->replication_password;
	replicationSource.slotName = config->replication_slot_name;
	replicationSource.maximumBackupRate = config->maximum_backup_rate;

	if (!standby_init_database(postgres, &replicationSource))
	{
		log_error("Failed initialise standby server, see above for details");
		return false;
	}

	return true;
}


/*
 * fsm_rewind_or_init is used when a new primary is available. First, try to
 * rewind. If that fails, do a pg_basebackup.
 */
bool
fsm_rewind_or_init(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	Monitor *monitor = &(keeper->monitor);
	LocalPostgresServer *postgres = &(keeper->postgres);
	ReplicationSource replicationSource = { 0 };
	int groupId = keeper->state.current_group;

	/* get the primary node to follow */
	if (!monitor_get_primary(monitor, config->formation, groupId,
							 &replicationSource.primaryNode))
	{
		log_error("Failed to initialise standby because get the primary node "
				  "from the monitor failed, see above for details");
		return false;
	}

	replicationSource.userName = PG_AUTOCTL_REPLICA_USERNAME;
	replicationSource.password = config->replication_password;
	replicationSource.slotName = config->replication_slot_name;
	replicationSource.maximumBackupRate = config->maximum_backup_rate;

	if (!primary_rewind_to_standby(postgres, &replicationSource))
	{
		log_warn("Failed to rewind demoted primary to standby, "
				 "trying pg_basebackup instead");

		if (!standby_init_database(postgres, &replicationSource))
		{
			log_error("Failed to become standby server, see above for details");
			return false;
		}
	}

	return true;
}


/*
 * fsm_prepare_standby_for_promotion used when the standby is asked to prepare
 * its own promotion.
 *
 * TODO: implement the prepare_promotion_walreceiver_timeout as follows:
 *
 *   We need to loop over the `ready_to_promote' until the standby is ready.
 *   This routine compare the time spent waiting to the setup:
 *
 *   prepare_promotion_walreceiver_timeout
 *
 *   The `ready_to_promote' routine eventually returns true.
 *
 *   Currently the keeper only supports Synchronous Replication so this timeout
 *   isn't necessary, that's why it's not implemented yet. The implementation
 *   needs to happen for async rep support.
 */
bool
fsm_prepare_standby_for_promotion(Keeper *keeper)
{
	log_debug("No support for async replication means we don't wait until "
			  "prepare_promotion_walreceiver_timeout (%ds)",
			  keeper->config.prepare_promotion_walreceiver);

	return true;
}


/*
 * fsm_suspend_standby is used when putting the standby in maintenance mode
 * (kernel upgrades, change of hardware, etc). Maintenance means that the user
 * now is driving the service, refrain from doing anything ourselves.
 */
bool
fsm_start_maintenance_on_standby(Keeper *keeper)
{
	return true;
}


/*
 * fsm_restart_standby is used when restarting standby after manual maintenance
 * is done.
 */
bool
fsm_restart_standby(Keeper *keeper)
{
	return fsm_start_postgres(keeper);
}


/*
 * The following actions are needed to promote a standby, and used in several
 * situations in the FSM transitions:
 *
 *    start_postgres
 * && promote_standby
 * && add_standby_to_hba
 * && create_replication_slot
 * && disable_synchronous_replication
 */
bool
fsm_promote_standby(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);
	bool other_node_missing_is_ok = true;

	if (!ensure_local_postgres_is_running(postgres))
	{
		log_error("Failed to promote postgres because the server could not "
				  "be started before promotion, see above for details");
		return false;
	}

	/*
	 * If postgres is no longer in recovery mode, standby_promote returns true
	 * immediately and therefore this function is idempotent.
	 */
	if (!standby_promote(postgres))
	{
		log_error("Failed to promote the local postgres server from standby "
				  "to single state, see above for details");
		return false;
	}

	/*
	 * The old primary will (hopefully) come back as a secondary and should
	 * be able to open a replication connection. We therefore need to do the
	 * same steps we take when going from single to wait_primary, namely to
	 * create a replication slot and add the other node to pg_hba.conf. These
	 * steps are implemented in fsm_prepare_replication.
	 */
	if (!prepare_replication(keeper, other_node_missing_is_ok))
	{
		/* prepare_replication logs relevant errors */
		return false;
	}

	if (!primary_disable_synchronous_replication(postgres))
	{
		log_error("Failed to disable synchronous replication after promotion, "
				  "see above for details");
		return false;
	}

	return true;
}
