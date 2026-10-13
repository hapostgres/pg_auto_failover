/*
 * src/bin/pg_autoctl/cli_restart.c
 *     Implementation of the pg_autoctl restart CLI, which restarts Postgres
 *     on the local node through pg_autoctl's own supervised process,
 *     optionally wrapping the operation with maintenance mode.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <getopt.h>
#include <inttypes.h>

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "fsm.h"
#include "keeper_config.h"
#include "keeper.h"
#include "log.h"
#include "monitor.h"

static bool optMaintenance = false;
static bool optAllowFailover = false;
static bool optForce = false;

static int cli_restart_postgres_getopts(int argc, char **argv);
static void cli_restart_postgres(int argc, char **argv);

static bool cli_restart_enable_maintenance(Keeper *keeper);
static bool cli_restart_disable_maintenance(Keeper *keeper);
static bool cli_restart_checkpoint_and_restart(Keeper *keeper);

CommandLine restart_postgres_command =
	make_command("postgres",
				 "Restart Postgres on this node, going through pg_autoctl's "
				 "own supervised process",
				 " [ --pgdata ] [ --maintenance [ --allow-failover ] | --force ]",
				 "  --pgdata          path to data directory\n"
				 "  --maintenance     Enable maintenance mode before stopping "
				 "Postgres, and disable it again once the restart is done. "
				 "Without this option, the node must already be in "
				 "maintenance.\n"
				 "  --allow-failover  Together with --maintenance, allow "
				 "restarting the primary: the monitor promotes a standby "
				 "first, same as `enable maintenance --allow-failover`. "
				 "Without it, --maintenance on a primary is refused.\n"
				 "  --force           Skip every state check (maintenance, "
				 "primary/standby) and restart unconditionally. Does not "
				 "touch maintenance mode. Mutually exclusive with "
				 "--maintenance and --allow-failover.\n",
				 cli_restart_postgres_getopts,
				 cli_restart_postgres);

static CommandLine *restart_subcommands[] = {
	&restart_postgres_command,
	NULL
};

CommandLine restart_commands =
	make_command_set("restart",
					 "Restart a pg_auto_failover service", NULL, NULL,
					 NULL, restart_subcommands);


/*
 * cli_restart_postgres_getopts parses the command line options for the
 * command `pg_autoctl restart postgres`.
 */
static int
cli_restart_postgres_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "maintenance", no_argument, NULL, 'M' },
		{ "allow-failover", no_argument, NULL, 'A' },
		{ "force", no_argument, NULL, 'F' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:MAFVvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(options.pgSetup.pgdata, optarg, MAXPGPATH);
				log_trace("--pgdata %s", options.pgSetup.pgdata);
				break;
			}

			case 'M':
			{
				/* { "maintenance", no_argument, NULL, 'M' }, */
				optMaintenance = true;
				log_trace("--maintenance");
				break;
			}

			case 'A':
			{
				/* { "allow-failover", no_argument, NULL, 'A' }, */
				optAllowFailover = true;
				log_trace("--allow-failover");
				break;
			}

			case 'F':
			{
				/* { "force", no_argument, NULL, 'F' }, */
				optForce = true;
				log_trace("--force");
				break;
			}

			case 'V':
			{
				/* keeper_cli_print_version prints version and exits. */
				keeper_cli_print_version(argc, argv);
				break;
			}

			case 'v':
			{
				++verboseCount;
				switch (verboseCount)
				{
					case 1:
					{
						log_set_level(LOG_INFO);
						break;
					}

					case 2:
					{
						log_set_level(LOG_DEBUG);
						break;
					}

					default:
					{
						log_set_level(LOG_TRACE);
						break;
					}
				}
				break;
			}

			case 'q':
			{
				log_set_level(LOG_ERROR);
				break;
			}

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
				break;
			}

			default:
			{
				/* getopt_long already wrote an error message */
				errors++;
				break;
			}
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (optForce && (optMaintenance || optAllowFailover))
	{
		log_fatal("Please use either --force, or --maintenance "
				  "(optionally with --allow-failover), not both: "
				  "--force skips maintenance mode entirely");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (optAllowFailover && !optMaintenance)
	{
		log_fatal("Option --allow-failover requires --maintenance");
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* now that we have the command line parameters, prepare the options */
	(void) prepare_keeper_options(&options);

	/* publish our option parsing in the global variable */
	keeperOptions = options;

	return optind;
}


/*
 * cli_restart_enable_maintenance enables maintenance mode on the local node,
 * the same way `pg_autoctl enable maintenance` does, and waits until the
 * node has reached the maintenance state.
 */
static bool
cli_restart_enable_maintenance(Keeper *keeper)
{
	char *channels[] = { "state", NULL };
	ConnectionRetryPolicy retryPolicy = { 0 };

	int64_t nodeId = keeper->state.current_node_id;
	bool isPrimary = keeper->state.current_role == PRIMARY_STATE;

	if (isPrimary && !optAllowFailover)
	{
		log_warn("Restarting Postgres on the primary requires entering "
				 "maintenance first, which causes a failover");
		log_fatal("Please use --allow-failover to allow the command "
				  "to proceed");
		return false;
	}

	if (!pgsql_listen(&(keeper->monitor.notificationClient), channels))
	{
		log_error("Failed to listen to state changes from the monitor");
		return false;
	}

	(void) pgsql_set_monitor_interactive_retry_policy(&retryPolicy);

	while (!pgsql_retry_policy_expired(&retryPolicy))
	{
		bool mayRetry = false;

		if (monitor_start_maintenance(&(keeper->monitor), nodeId, &mayRetry))
		{
			break;
		}

		if (!mayRetry)
		{
			log_fatal("Failed to enable maintenance of node %" PRId64
					  " on the monitor, see above for details",
					  nodeId);
			return false;
		}

		int sleepTimeMs = pgsql_compute_connection_retry_sleep_time(&retryPolicy);

		log_warn("Failed to enable maintenance of node %" PRId64
				 " on the monitor, retrying in %d ms.",
				 nodeId, sleepTimeMs);

		(void) pg_usleep(sleepTimeMs * 1000);
	}

	if (pgsql_retry_policy_expired(&retryPolicy))
	{
		log_fatal("Failed to enable maintenance, see above for details");
		return false;
	}

	NodeState targetStates[] = { MAINTENANCE_STATE };

	/*
	 * When --allow-failover is used on the primary, the monitor must first
	 * promote a secondary (up to 60s) and then assign maintenance to this
	 * node (another 60s). Use 3x the base timeout so both phases can
	 * complete within the single wait window, same as `enable maintenance`.
	 */
	int waitTimeoutSecs = (optAllowFailover && isPrimary)
						  ? 3 * PG_AUTOCTL_LISTEN_NOTIFICATIONS_TIMEOUT
						  : PG_AUTOCTL_LISTEN_NOTIFICATIONS_TIMEOUT;

	if (!monitor_wait_until_node_reported_state(
			&(keeper->monitor),
			keeper->config.formation,
			keeper->config.groupId,
			nodeId,
			keeper->config.pgSetup.pgKind,
			targetStates,
			lengthof(targetStates),
			waitTimeoutSecs))
	{
		log_error("Failed to wait until the node reached the "
				  "maintenance state");
		return false;
	}

	return true;
}


/*
 * cli_restart_disable_maintenance disables maintenance mode on the local
 * node, the same way `pg_autoctl disable maintenance` does, and waits until
 * the node has rejoined as a secondary or primary.
 */
static bool
cli_restart_disable_maintenance(Keeper *keeper)
{
	char *channels[] = { "state", NULL };
	ConnectionRetryPolicy retryPolicy = { 0 };

	int64_t nodeId = keeper->state.current_node_id;

	if (!pgsql_listen(&(keeper->monitor.notificationClient), channels))
	{
		log_error("Failed to listen to state changes from the monitor");
		return false;
	}

	(void) pgsql_set_monitor_interactive_retry_policy(&retryPolicy);

	while (!pgsql_retry_policy_expired(&retryPolicy))
	{
		bool mayRetry = false;

		if (monitor_stop_maintenance(&(keeper->monitor), nodeId, &mayRetry))
		{
			break;
		}

		if (!mayRetry)
		{
			log_fatal("Failed to disable maintenance of node %" PRId64
					  " on the monitor, see above for details",
					  nodeId);
			return false;
		}

		int sleepTimeMs = pgsql_compute_connection_retry_sleep_time(&retryPolicy);

		log_warn("Failed to disable maintenance of node %" PRId64
				 " on the monitor, retrying in %d ms.",
				 nodeId, sleepTimeMs);

		(void) pg_usleep(sleepTimeMs * 1000);
	}

	if (pgsql_retry_policy_expired(&retryPolicy))
	{
		log_fatal("Failed to disable maintenance, see above for details");
		return false;
	}

	NodeState targetStates[] = { SECONDARY_STATE, PRIMARY_STATE };

	if (!monitor_wait_until_node_reported_state(
			&(keeper->monitor),
			keeper->config.formation,
			keeper->config.groupId,
			nodeId,
			keeper->config.pgSetup.pgKind,
			targetStates,
			lengthof(targetStates),
			PG_AUTOCTL_LISTEN_NOTIFICATIONS_TIMEOUT))
	{
		log_error("Failed to wait until the node reached the "
				  "secondary or primary state");
		return false;
	}

	return true;
}


/*
 * cli_restart_checkpoint_and_restart stops Postgres the smart way -- the
 * same couple of CHECKPOINTs some FSM transitions already perform before a
 * controlled shutdown (fsm_checkpoint_and_stop_postgres(), used for e.g.
 * entering maintenance on a primary), so that the final CHECKPOINT Postgres
 * itself issues at shutdown has as little work left to do as possible --
 * and then starts it back up through pg_autoctl's own supervised process.
 */
static bool
cli_restart_checkpoint_and_restart(Keeper *keeper)
{
	bool updateRetries = false;

	if (!fsm_checkpoint_and_stop_postgres(keeper))
	{
		log_error("Failed to stop Postgres, see above for details");
		return false;
	}

	if (!keeper_ensure_postgres_is_running(keeper, updateRetries))
	{
		log_error("Failed to restart Postgres, see above for details");
		return false;
	}

	log_info("Postgres has been restarted at \"%s\"",
			 keeper->postgres.postgresSetup.pgdata);

	return true;
}


/*
 * cli_restart_postgres implements `pg_autoctl restart postgres`.
 */
static void
cli_restart_postgres(int argc, char **argv)
{
	Keeper keeper = { 0 };

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	keeper.config = keeperOptions;

	(void) exit_unless_role_is_keeper(&(keeper.config));

	if (!keeper_config_read_file(&(keeper.config),
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_init(&keeper, &keeper.config))
	{
		log_fatal("Failed to initialize keeper, see above for details");
		exit(EXIT_CODE_KEEPER);
	}

	if (!monitor_init(&(keeper.monitor), keeper.config.monitor_pguri))
	{
		log_fatal("Failed to initialize the monitor connection, "
				  "see above for details.");
		exit(EXIT_CODE_MONITOR);
	}

	if (optForce)
	{
		log_warn("--force given: skipping maintenance-state and "
				 "primary/standby checks");
	}
	else
	{
		bool alreadyInMaintenance =
			keeper.state.current_role == MAINTENANCE_STATE;

		if (!alreadyInMaintenance)
		{
			if (!optMaintenance)
			{
				log_fatal("This node is not in maintenance. Use "
						  "`pg_autoctl enable maintenance` first, pass "
						  "--maintenance to have this command manage it "
						  "automatically, or --force to skip this check "
						  "entirely.");
				exit(EXIT_CODE_BAD_STATE);
			}

			if (!cli_restart_enable_maintenance(&keeper))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_MONITOR);
			}
		}
	}

	if (!cli_restart_checkpoint_and_restart(&keeper))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	if (optMaintenance)
	{
		/*
		 * --maintenance owns both ends of the operation: whether we just
		 * entered maintenance above or the node was already in it before
		 * this command ran, disable it now that the restart is done.
		 */
		if (!cli_restart_disable_maintenance(&keeper))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_MONITOR);
		}

		log_info("Node has rejoined after the restart, maintenance "
				 "mode is now disabled");
	}
	else if (!optForce)
	{
		log_info("This node remains in maintenance mode; run "
				 "`pg_autoctl disable maintenance` when ready.");
	}
}
