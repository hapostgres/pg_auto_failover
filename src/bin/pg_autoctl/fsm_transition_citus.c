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
 */

#include <assert.h>
#include <unistd.h>
#include <time.h>

#include "coordinator.h"
#include "defaults.h"
#include "env_utils.h"
#include "pgctl.h"
#include "fsm.h"
#include "keeper.h"
#include "keeper_pg_init.h"
#include "log.h"
#include "monitor.h"
#include "primary_standby.h"
#include "state.h"

#define PLACEHOLDER_FOR_COMMENT ""

static bool ensure_hostname_is_current_on_coordinator(Keeper *keeper);


/*
 * fsm_citus_coordinator_init_primary initializes a primary coordinator node in
 * a Citus formation. After doing the usual initialization steps as per the
 * non-citus version of the FSM, the coordinator node registers itself to the
 * Citus nodes metadata.
 */
bool
fsm_citus_coordinator_init_primary(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);

	CoordinatorNodeAddress coordinatorNodeAddress = { 0 };
	Coordinator coordinator = { 0 };
	int nodeid = -1;

	if (!fsm_init_primary(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * Only Citus workers have more work to do, coordinator are ok. To add
	 * coordinator to the metadata, users can call "activate" subcommand
	 * for the coordinator.
	 */
	if (keeper->postgres.pgKind != NODE_KIND_CITUS_COORDINATOR)
	{
		log_error("BUG: fsm_citus_coordinator_init_primary called for "
				  "node kind %s",
				  nodeKindToString(keeper->postgres.pgKind));
		return false;
	}

	/*
	 * We now have a coordinator to talk to: add ourselves as inactive.
	 */
	coordinatorNodeAddress.node.port = keeper->config.pgSetup.pgport;

	strlcpy(coordinatorNodeAddress.node.name,
			keeper->config.name,
			sizeof(coordinatorNodeAddress.node.name));

	strlcpy(coordinatorNodeAddress.node.host,
			keeper->config.hostname,
			sizeof(coordinatorNodeAddress.node.host));


	if (!coordinator_init(&coordinator, &(coordinatorNodeAddress.node), keeper))
	{
		log_fatal("Failed to contact the coordinator because its URL is invalid, "
				  "see above for details");
		return false;
	}

	if (!coordinator_add_node(&coordinator, keeper, &nodeid))
	{
		/*
		 * master_add_inactive_node() is idempotent: if the node already has
		 * been added, nothing changes, in particular if the node is active
		 * already then the function happily let the node active.
		 */
		log_fatal("Failed to add current node to the Citus coordinator, "
				  "see above for details");
		return false;
	}

	log_info("Added coordinator node %s:%d in formation \"%s\" to itself",
			 keeper->config.hostname,
			 keeper->config.pgSetup.pgport,
			 config->formation);

	return true;
}


/*
 * fsm_citus_init_primary initializes a primary worker node in a Citus
 * formation. After doing the usual initialization steps as per the non-citus
 * version of the FSM, the worker node must be added to Citus.
 *
 * We call master_add_inactive_node() on the coordinator, then we call
 * master_activate_node(). It might be that the coordinator node isn't ready
 * yet, in which case we return false, and the main loop is going to retry that
 * transition every 5s for us.
 */
bool
fsm_citus_worker_init_primary(Keeper *keeper)
{
	Monitor *monitor = &(keeper->monitor);
	KeeperConfig *config = &(keeper->config);
	PostgresSetup pgSetup = config->pgSetup;

	CoordinatorNodeAddress coordinatorNodeAddress = { 0 };
	Coordinator coordinator = { 0 };
	int nodeid = -1;

	int retries = config->citus_coordinator_wait_max_retries;
	int timeout = config->citus_coordinator_wait_timeout;
	uint64_t startTime = 0;
	int attempts = 0;
	bool couldGetCoordinator = false;

	if (!fsm_init_primary(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * Only Citus workers have more work to do, coordinator are ok. To add
	 * coordinator to the metadata, users can call "activate" subcommand
	 * for the coordinator.
	 */
	if (keeper->postgres.pgKind != NODE_KIND_CITUS_WORKER)
	{
		return true;
	}

	startTime = time(NULL);

	while (!couldGetCoordinator)
	{
		uint64_t currentTime = time(NULL);
		int timeDiff = (int) (currentTime - startTime);

		++attempts;

		if (monitor_get_coordinator(monitor, config->formation,
									&coordinatorNodeAddress))
		{
			log_debug("Coordinator is available for formation \"%s\" at \"%s:%d\".",
					  config->formation,
					  coordinatorNodeAddress.node.host,
					  coordinatorNodeAddress.node.port);
			couldGetCoordinator = true;
		}
		else if (attempts == 1)
		{
			log_warn("Failed to get the coordinator for formation \"%s\" for "
					 "the first time. Retrying every %d seconds for up to %d "
					 "seconds or %d attempts for the coordinator to become "
					 "available.",
					 config->formation, PG_AUTOCTL_KEEPER_SLEEP_TIME,
					 timeout, retries);
		}
		else if (attempts >= retries || timeDiff > timeout)
		{
			log_error("Failed to get the coordinator for formation \"%s\" "
					  "from the monitor at %s after %d attempts in last "
					  "%d seconds.",
					  config->formation, config->monitor_pguri,
					  attempts, (int) timeDiff);
			return false;
		}

		if (!couldGetCoordinator)
		{
			sleep(PG_AUTOCTL_KEEPER_SLEEP_TIME);
		}
	}

	/*
	 * We now have a coordinator to talk to: add ourselves as inactive.
	 */
	if (!coordinator_init(&coordinator, &(coordinatorNodeAddress.node), keeper))
	{
		log_fatal("Failed to contact the coordinator because its URL is invalid, "
				  "see above for details");
		return false;
	}

	/* use a special connection retry policy for initialisation */
	(void) pgsql_set_init_retry_policy(&(coordinator.pgsql.retryPolicy));

	if (!coordinator_add_inactive_node(&coordinator, keeper, &nodeid))
	{
		/*
		 * master_add_inactive_node() is idempotent: if the node already has
		 * been added, nothing changes, in particular if the node is active
		 * already then the function happily let the node active.
		 */
		log_fatal("Failed to add current node to the Citus coordinator, "
				  "see above for details");
		return false;
	}

	log_info("Added inactive node %s:%d in formation \"%s\" at coordinator %s:%d",
			 keeper->config.hostname, keeper->config.pgSetup.pgport,
			 config->formation,
			 coordinator.node.host, coordinator.node.port);

	/*
	 * If there is a proxyport add it to pg_dist_poolinfo
	 */
	if (pgSetup.proxyport > 0)
	{
		if (!coordinator_upsert_poolinfo_port(&coordinator, keeper))
		{
			log_fatal("Failed to add proxyport to pg_dist_poolinfo, "
					  "see above for details");
			return false;
		}

		log_info("Added proxyport %d to pg_dist_poolinfo", pgSetup.proxyport);
	}

	/*
	 * And activate the new node now.
	 *
	 * Node activation may fail because of database schema using user defined
	 * data types or lacking constraints, in which case we want to succeed the
	 * init process and allow users to complete activation of the node later.
	 *
	 * As of Citus 10 (and some earlier releases) SQL objects dependencies are
	 * now fully tracked by Citus and the workers activation is supposed to
	 * "just work". The most plausible error is related to HBA communication
	 * from the coordinator to the worker. We should then fail the
	 * initialisation and try again later.
	 */
	if (!coordinator_activate_node(&coordinator, keeper, &nodeid))
	{
		log_error("Failed to activate current node to the Citus coordinator, "
				  "see above for details");

		return false;
	}

	log_info("Activated node %s:%d in formation \"%s\" coordinator %s:%d",
			 keeper->config.hostname, keeper->config.pgSetup.pgport,
			 config->formation,
			 coordinator.node.host, coordinator.node.port);

	/*
	 * The previous coordinator functions we called didn't close the
	 * connection, so that we could do three SQL calls in a single connection.
	 * It's now time to close the coordinator connection.
	 */
	pgsql_finish(&coordinator.pgsql);

	return true;
}


/*
 * fsm_citus_resume_as_primary is used when the local node was demoted after a
 * failure, but standby was forcibly removed.
 *
 * When the current node is a Citus Worker, we need to ensure that the current
 * node is still registered on the coordinator: we could be in the middle of a
 * transition to the standby and need to make the primary back the
 * coordinator's worker node.
 */
bool
fsm_citus_worker_resume_as_primary(Keeper *keeper)
{
	if (!fsm_resume_as_primary(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	return ensure_hostname_is_current_on_coordinator(keeper);
}


/*
 * ensure_hostname_is_current_on_coordinator verifies that on the coordinator
 * the current hostname for our group is the one of this primary server. This
 * is needed when a failover is aborted in the middle of it because the
 * secondary disappeared while we were trying to promote it.
 *
 * The transitions where that could happen are:
 *
 *  1.         demoted ➜ single
 *  2.  demote_timeout ➜ single
 *  3.        draining ➜ single
 *
 * There might be a "master_update_node ${groupid}" prepared transaction in
 * flight on the coordinator, in which case we want to rollback that
 * transaction, which should bring us back to having the proper hostname
 * registered.
 *
 * When no master_update_node transaction has been prepared, we need to ensure
 * the current node is registered on the coordinator: the prepared transaction
 * might have been committed before we lost the secondary node.
 */
static bool
ensure_hostname_is_current_on_coordinator(Keeper *keeper)
{
	KeeperStateData *state = &(keeper->state);

	Coordinator coordinator = { 0 };
	char transactionName[PREPARED_TRANSACTION_NAMELEN];
	bool transactionHasBeenPrepared;

	/*
	 * This function assumes that we are dealing with a Citus worker node that
	 * has been assigned the SINGLE goal state. Assert() that it's true.
	 */
	assert(keeper->postgres.pgKind == NODE_KIND_CITUS_WORKER);
	assert(state->assigned_role == SINGLE_STATE);

	switch (state->current_role)
	{
		case DEMOTED_STATE:
		case DEMOTE_TIMEOUT_STATE:
		case DRAINING_STATE:
		{
			/* everything is fine here, as expected */
			break;
		}

		default:
		{
			log_error("BUG: ensure_hostname_is_current_on_coordinator called "
					  "with current role \"%s\".",
					  NodeStateToString(state->current_role));
			return false;
		}
	}

	/*
	 * Ok so we know we're in the expected situation, in the middle of a
	 * transition where the primary was supposed to be DEMOTEd, but now we've
	 * lost the secondary, and we need to bring the primary back to SINGLE.
	 */
	if (!coordinator_init_from_monitor(&coordinator, keeper))
	{
		log_error("Failed to connect to the coordinator node at %s:%d, "
				  "see above for details",
				  coordinator.node.host, coordinator.node.port);
		return false;
	}

	GetPreparedTransactionName(state->current_group, transactionName);

	if (!coordinator_udpate_node_transaction_is_prepared(
			&coordinator, keeper, &transactionHasBeenPrepared))
	{
		/* errors have already been logged */
		return false;
	}

	if (transactionHasBeenPrepared)
	{
		if (!coordinator_update_node_rollback(&coordinator, keeper))
		{
			/* errors have already been logged */
			return false;
		}

		/*
		 * The prepared transaction was doing master_update_node() to install
		 * the secondary as the registered hostname at the coordinator. We just
		 * did a ROLLBACK to that transaction, so we're back to having the
		 * current primary hostname in place.
		 */
		return true;
	}
	else
	{
		/*
		 * We lost the secondary after we began the failover, and either before
		 * it could PREPARE the master_update_node transaction on the
		 * coordinator, or after it did COMMIT this transaction.
		 *
		 * In both situations, we're good to call master_update_node() again:
		 * it's a noop when the target name is the same as the current one.
		 *
		 * We don't strictly need to do that in a 2PC transaction here. We need
		 * to take care of conflicting activity though, so we might as well
		 * re-use the existing support for that.
		 */
		if (!coordinator_update_node_prepare(&coordinator, keeper))
		{
			/* errors have already been logged */
			return false;
		}

		if (!coordinator_update_node_commit(&coordinator, keeper))
		{
			/* errors have already been logged */
			return false;
		}
	}

	/* disconnect from PostgreSQL on the coordinator now */
	pgsql_finish(&(coordinator.pgsql));

	return false;
}


/*
 * fsm_promote_standby_to_single is used when the primary was forcibly
 * removed, which means the standby becomes the single node and should
 * be promoted.
 *
 * start_postgres && promote_standby && disable_synchronous_replication
 */
bool
fsm_citus_coordinator_promote_standby_to_single(Keeper *keeper)
{
	/* errors are already logged in the functions called here */
	return fsm_promote_standby(keeper) &&
		   fsm_citus_coordinator_master_update_itself(keeper);
}


/*
 * fsm_citus_worker_promote_standby_to_single is used when the primary was
 * forcibly removed, which means the standby becomes the single node and should
 * be promoted.
 *
 * This is a variant of fsm_promote_standby_to_single that only applies to
 * Citus worker nodes, where we also have some work to do with the coordinator.
 */
bool
fsm_citus_worker_promote_standby_to_single(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	KeeperStateData *state = &(keeper->state);
	Coordinator coordinator = { 0 };
	char transactionName[PREPARED_TRANSACTION_NAMELEN];

	if (keeper->postgres.pgKind != NODE_KIND_CITUS_WORKER)
	{
		log_error("BUG: fsm_citus_worker_promote_standby_to_single called "
				  "with a node kind that is not a worker: \"%s\"",
				  nodeKindToString(keeper->postgres.pgKind));
		return false;
	}

	/*
	 * When promoting a standby directly to single, we need to update the
	 * coordinator's metadata by calling master_update_node(). First thing we
	 * do in that case is PREPARE TRANSACTION the master_update_node() change,
	 * blocking writes to this worker node on the coordinator, and then at the
	 * end of this transition we COMMIT PREPARED.
	 *
	 * The removal of the primary node might also happen while we are already
	 * in the STOP_REPLICATION_STATE, in which case the master_update_node
	 * transaction has already been prepared.
	 */
	if (!coordinator_init_from_monitor(&coordinator, keeper))
	{
		log_error("Failed to connect to the coordinator node at %s:%d, "
				  "see above for details",
				  coordinator.node.host, coordinator.node.port);
		return false;
	}

	GetPreparedTransactionName(state->current_group, transactionName);

	log_info("Preparing failover to %s:%d on coordinator %s:%d: "
			 "PREPARE TRANSACTION \"%s\"",
			 config->hostname, config->pgSetup.pgport,
			 coordinator.node.host, coordinator.node.port,
			 transactionName);

	if (!coordinator_update_node_prepare(&coordinator, keeper))
	{
		log_error("Failed to call master_update_node, "
				  "see above for details");
		return false;
	}

	log_info("Coordinator is now blocking writes to groupId %d",
			 state->current_group);

	/*
	 * Now proceed with promoting the local Postgres standby node.
	 */
	if (!fsm_promote_standby(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	log_info("Finishing failover on coordinator %s:%d: COMMIT PREPARED \"%s\"",
			 coordinator.node.host, coordinator.node.port, transactionName);

	if (!coordinator_update_node_commit(&coordinator, keeper))
	{
		log_error("Failed to commit prepared transaction for "
				  "master_update_node() on the coordinator, "
				  "see above for details");
		return false;
	}

	return true;
}


/*
 * fsm_citus_cleanup_and_resume_as_primary cleans-up the replication setting
 * and start the local node as primary. It's called after a fast-forward
 * operation.
 */
bool
fsm_citus_cleanup_and_resume_as_primary(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);

	if (!standby_cleanup_as_primary(postgres))
	{
		log_error("Failed to cleanup replication settings and restart Postgres "
				  "to continue as a primary, see above for details");
		return false;
	}

	if (!keeper_restart_postgres(keeper))
	{
		log_error("Failed to restart Postgres after changing its "
				  "primary conninfo, see above for details");
		return false;
	}

	/* now prepare and commit the call to master_update_node() */
	return fsm_citus_worker_prepare_standby_for_promotion(keeper);
}


/*
 * fsm_citus_coordinator_master_update_itself is used when the primary was
 * forcibly removed, which means the standby becomes the single node and should
 * be promoted.
 *
 * This is a variant of fsm_promote_standby_to_single that only applies to
 * Citus coordinator nodes, where we might have to call master_update_node.
 */
bool
fsm_citus_coordinator_master_update_itself(Keeper *keeper)
{
	Coordinator coordinator = { 0 };
	bool isRegistered = false;

	if (keeper->postgres.pgKind != NODE_KIND_CITUS_COORDINATOR)
	{
		log_error("BUG: fsm_citus_coordinator_master_update_itself called "
				  "with a node kind that is not a coordinator: \"%s\"",
				  nodeKindToString(keeper->postgres.pgKind));
		return false;
	}

	/*
	 * The Citus coordinator can be asked to host a copy of the reference
	 * tables, enabling advanced features. Users can opt-in to that with
	 * SELECT master_add_node('coordinator-host', 5432, groupid:= 0).
	 *
	 * At coordinator failover we then should run the master_update_node
	 * query for the coordinator itself, in case it might have been
	 * registered in pg_dist_node with the worker nodes.
	 *
	 * We don't strictly need to do that in a 2PC transaction here. We need
	 * to take care of conflicting activity though, so we might as well
	 * re-use the existing support for that.
	 */
	if (!coordinator_init_from_keeper(&coordinator, keeper))
	{
		log_error("Failed to add the coordinator node to itself, "
				  "see above for details");
		return false;
	}

	if (!coordinator_node_is_registered(&coordinator, &isRegistered))
	{
		/* errors have already been logged */
		return false;
	}

	if (isRegistered)
	{
		if (!coordinator_update_node_prepare(&coordinator, keeper))
		{
			/* errors have already been logged */
			return false;
		}

		if (!coordinator_update_node_commit(&coordinator, keeper))
		{
			/* errors have already been logged */
			return false;
		}
	}

	return true;
}


/*
 * fsm_citus_worker_stop_replication is used to forcefully stop replication, in
 * case the primary is on the other side of a network split.
 */
bool
fsm_citus_worker_stop_replication(Keeper *keeper)
{
	if (keeper->postgres.pgKind == NODE_KIND_CITUS_WORKER)
	{
		/*
		 * A Citus Worker node only receives SQL traffic from the coordinator,
		 * and in the failover process the keeper blocks all writes to the
		 * local node by means of calling master_update_node() on the
		 * coordinator.
		 *
		 * Which means that we don't have to worry about split-brains
		 * situations, because we control the client connections, and stopped
		 * writes already. In that case we may already promote the local node
		 * to being the new primary.
		 */
		log_info("The coordinator is no longer sending writes to the "
				 "old primary worker, proceeding with promotion");

		return fsm_promote_standby(keeper);
	}
	else
	{
		return fsm_stop_replication(keeper);
	}
}


/*
 * fsm_citus_worker_promote_standby_to_primary is used when the standby should
 * become the new primary. It also prepares for the old primary to become the
 * new standby.
 *
 * The promotion of the standby has already happened in the previous
 * transition:
 *
 *  1.         secondary ➜ prepare_promotion : block writes
 *  2. prepare_promotion ➜ stop_replication  : promote
 *  3.  stop_replication ➜ wait_primary      : resume writes
 *
 * On a Citus worker, resuming writes is done through committing the two-phase
 * commit transaction around master_update_node() on the coordinator.
 *
 * On a standalone PostgreSQL instance and on a Citus coordinator, resuming
 * writes is done by setting default_transaction_read_only to off, thus
 * allowing libpq to establish connections when target_session_attrs is
 * read-write.
 */
bool
fsm_citus_worker_promote_standby_to_primary(Keeper *keeper)
{
	Coordinator coordinator = { 0 };
	char transactionName[PREPARED_TRANSACTION_NAMELEN];

	GetPreparedTransactionName(keeper->state.current_group, transactionName);

	if (!coordinator_init_from_monitor(&coordinator, keeper))
	{
		log_error("Failed to commit prepared transaction \"%s\""
				  "on the Citus coordinator %s:%d, "
				  "see above for details",
				  transactionName,
				  coordinator.node.host, coordinator.node.port);
		return false;
	}

	log_info("Finishing failover on coordinator %s:%d: "
			 "COMMIT PREPARED \"%s\"",
			 coordinator.node.host, coordinator.node.port, transactionName);

	if (!coordinator_update_node_commit(&coordinator, keeper))
	{
		log_error("Failed to commit prepared transaction \"%s\""
				  "on the Citus coordinator %s:%d, "
				  "see above for details",
				  transactionName,
				  coordinator.node.host, coordinator.node.port);
		return false;
	}

	return true;
}


/*
 * fsm_citus_coordinator_promote_standby_to_primary is used when the
 * coordinator standby node should become the new primary. It also prepares for
 * the old primary to become the new standby.
 */
bool
fsm_citus_coordinator_promote_standby_to_primary(Keeper *keeper)
{
	/* errors are already logged in the functions called here */
	return fsm_promote_standby_to_primary(keeper) &&
		   fsm_citus_coordinator_master_update_itself(keeper);
}


/*
 * fsm_promote_citus_worker_standby is used when orchestrating the failover of
 * a Citus Worker node: in that case, all the writes happen through the Citus
 * coordinator, and we have blocked writes in the transition from SECONDARY to
 * PREPARE_PROMOTION, see `fsm_prepare_standby_for_promotion'.
 *
 * So because writes are blocked, there's no possibility of split brain, and we
 * can proceed straight from PREPARE_PROMOTION to WAIT_PRIMARY in this case.
 *
 * So we're doing the following:
 *
 *    start_postgres
 * && promote_standby
 * && add_standby_to_hba
 * && create_replication_slot
 * && disable_synchronous_replication
 *
 * When managing a Citus worker, this transition is the proper time to COMMIT
 * PREPARED the master_update_node() transaction on the coordinator too.
 */
bool
fsm_citus_worker_promote_standby(Keeper *keeper)
{
	Coordinator coordinator = { 0 };
	char transactionName[PREPARED_TRANSACTION_NAMELEN];

	if (!fsm_promote_standby(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * Citus worker nodes need to deal with master_update_node, other nodes are
	 * done with the transition now.
	 */
	if (keeper->postgres.pgKind != NODE_KIND_CITUS_WORKER)
	{
		return true;
	}

	GetPreparedTransactionName(keeper->state.current_group, transactionName);

	if (!coordinator_init_from_monitor(&coordinator, keeper))
	{
		log_error("Failed to commit prepared transaction \"%s\""
				  "on the Citus coordinator %s:%d, "
				  "see above for details",
				  transactionName,
				  coordinator.node.host, coordinator.node.port);
		return false;
	}

	log_info("Finishing failover on coordinator %s:%d: "
			 "COMMIT PREPARED \"%s\"",
			 coordinator.node.host, coordinator.node.port, transactionName);

	if (!coordinator_update_node_commit(&coordinator, keeper))
	{
		log_error("Failed to commit prepared transaction \"%s\""
				  "on the Citus coordinator %s:%d, "
				  "see above for details",
				  transactionName,
				  coordinator.node.host, coordinator.node.port);
		return false;
	}
	return true;
}


/*
 * fsm_citus_worker_prepare_standby_for_promotion is used when the standby is
 * asked to prepare its own promotion.
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
 *
 * When the local node is a Citus worker in a formation, now (going from
 * SECONDARY_STATE to PREP_PROMOTION_STATE) is the time to prepare a Two-Phase
 * Commit transaction where we call master_update_node() on the coordinator.
 *
 * The transaction is then commited when going from STOP_REPLICATION to
 * WAIT_PRIMARY via the transition function fsm_promote_standby_to_primary.
 */
bool
fsm_citus_worker_prepare_standby_for_promotion(Keeper *keeper)
{
	log_debug("No support for async replication means we don't wait until "
			  "prepare_promotion_walreceiver_timeout (%ds)",
			  keeper->config.prepare_promotion_walreceiver);

	if (keeper->postgres.pgKind == NODE_KIND_CITUS_WORKER)
	{
		KeeperConfig *config = &(keeper->config);
		KeeperStateData *state = &(keeper->state);
		Coordinator coordinator = { 0 };
		char transactionName[PREPARED_TRANSACTION_NAMELEN] = { 0 };

		/*
		 * Get the current coordinator node from the monitor, then prepare our
		 * master_update_node() change there. Failure to contact either the
		 * monitor or the coordinator will prevent this FSM transition to ever
		 * be successful in case of handling a Citus worker.
		 *
		 * Failing over to the worker's standby without calling
		 * master_update_node() on the coordinator would result in a broken
		 * Citus formation: the coordinator would still use the old primary
		 * node (not available anymore) thus failing both reads and writes with
		 * connections timeout or other errors.
		 *
		 * Worse, if we fail to lock write on the coordinator now, then we
		 * might cause a split brain situation for this worker. Better fail to
		 * transition and then failover than implement split brain.
		 */
		if (!coordinator_init_from_monitor(&coordinator, keeper))
		{
			/* that would be very surprising at this point */
			log_error("Failed to block writes to the current primary node for "
					  "the local Citus worker on the coordinator, "
					  "see above for details");
			return false;
		}

		GetPreparedTransactionName(state->current_group, transactionName);

		log_info("Preparing failover to node %d \"%s\" (%s:%d) in group %d "
				 "on coordinator %s:%d: PREPARE TRANSACTION \"%s\"",
				 keeper->state.current_node_id,
				 config->name,
				 config->hostname,
				 config->pgSetup.pgport,
				 keeper->state.current_group,
				 coordinator.node.host,
				 coordinator.node.port,
				 transactionName);

		/*
		 * The transaction name is built in coordinator_update_node_prepare()
		 * and saved in the keeper's state.preparedTransactionName.
		 */
		if (!coordinator_update_node_prepare(&coordinator, keeper))
		{
			log_error("Failed to call master_update_node, see above for details");
			return false;
		}

		log_info("Coordinator is now blocking writes to groupId %d",
				 state->current_group);
	}

	return true;
}


/*
 * fsm_citus_maintain_replication_slots is used when going from CATCHINGUP to
 * SECONDARY, to create missing replication slots. We want to maintain a
 * replication slot for each of the other nodes in the system, so that we make
 * sure we have the WAL bytes around when a standby nodes has to follow a new
 * primary, after failover.
 *
 * When handling a citus worker node that is a citus secondary (read replica),
 * we also need to register the node on the coordinator at this point.
 */
bool
fsm_citus_maintain_replication_slots(Keeper *keeper)
{
	Coordinator coordinator = { 0 };
	bool isRegistered = false;

	if (!fsm_prepare_for_secondary(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	/* on non-citus nodes, we are done now */
	if (!IS_CITUS_INSTANCE_KIND(keeper->postgres.pgKind))
	{
		return true;
	}

	if (!coordinator_init_from_monitor(&coordinator, keeper))
	{
		/* errors have already been logged */
		return false;
	}

	if (keeper->postgres.pgKind == NODE_KIND_CITUS_COORDINATOR)
	{
		if (!coordinator_node_is_registered(&coordinator, &isRegistered))
		{
			/* errors have already been logged */
			return false;
		}
	}

	if (keeper->postgres.pgKind == NODE_KIND_CITUS_WORKER || isRegistered)
	{
		KeeperConfig *config = &(keeper->config);
		KeeperStateData *state = &(keeper->state);

		int nodeid;

		if (config->citusRole == CITUS_ROLE_PRIMARY)
		{
			return true;
		}

		log_info("Adding node %d in group %d as a citus secondary",
				 state->current_node_id,
				 state->current_group);

		if (!coordinator_add_node(&coordinator, keeper, &nodeid))
		{
			log_error("Failed to add node %d in group %d as a citus secondary "
					  "on the citus coordinator at %s:%d",
					  state->current_node_id,
					  state->current_group,
					  coordinator.node.host,
					  coordinator.node.port);
			return false;
		}
	}

	return true;
}


/*
 * When dropping a Citus node we need to take extra actions and remove the node
 * from the coordinator... if the node has been registered there. Several
 * situations needs to be considered:
 *
 * - dropping a node that is not registered on the coordinator requires no
 *   extra action, we're good
 *
 * - dropping a worker primary node is already handled with the elected
 *   secondary node calling master_update_node, and calling master_remove_node
 *   concurrently may cause race condition hazards to the master_update_node
 *   code path
 *
 * - dropping a registered coordinator (see cluster_name and read replicas)
 *   requires dropping the node from the primary coordinator
 *
 * - dropping a worker secondary node that is registered on the coordinator
 *   with a non-default cluster_name requires dropping the node from the
 *   primary coordinator
 *
 * - when HA is disabled or unused and a SINGLE worker node is dropped, then
 *   removing the entry from the coordinator is required too
 *
 * All considered, it is left to the "ensure drop node" hook implemented in
 * citus_remove_dropped_nodes() to clean-up the coordinator entries.
 */
bool
fsm_citus_drop_node(Keeper *keeper)
{
	return fsm_drop_node(keeper);
}
