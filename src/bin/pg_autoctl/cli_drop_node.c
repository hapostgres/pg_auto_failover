/*
 * src/bin/pg_autoctl/cli_drop_node.c
 *     Implementation of the pg_autoctl create and pg_autoctl drop CLI for the
 *     pg_auto_failover nodes (monitor, coordinator, worker, postgres).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "commandline.h"
#include "env_utils.h"
#include "defaults.h"
#include "fsm.h"
#include "ini_file.h"
#include "ipaddr.h"
#include "keeper_config.h"
#include "keeper_pg_init.h"
#include "keeper.h"
#include "monitor.h"
#include "monitor_config.h"
#include "monitor_pg_init.h"
#include "pgctl.h"
#include "pghba.h"
#include "pidfile.h"
#include "primary_standby.h"
#include "service_keeper.h"
#include "service_keeper_init.h"
#include "service_monitor.h"
#include "service_monitor_init.h"
#include "signals.h"
#include "string_utils.h"

/*
 * Global variables that we're going to use to "communicate" in between getopts
 * functions and their command implementation. We can't pass parameters around.
 */
bool dropAndDestroy = false;
static bool dropForce = false;

static void cli_drop_monitor(int argc, char **argv);

static void cli_drop_local_monitor(MonitorConfig *mconfig, bool dropAndDestroy);

static void cli_drop_node_with_monitor_disabled(KeeperConfig *config,
												bool dropAndDestroy);
static void cli_drop_node_files_and_directories(KeeperConfig *config);
static void stop_postgres_and_remove_pgdata_and_config(ConfigFilePaths *pathnames,
													   PostgresSetup *pgSetup);

static void cli_drop_node_from_monitor_and_wait(KeeperConfig *config);

CommandLine drop_monitor_command =
	make_command("monitor",
				 "Drop the pg_auto_failover monitor",
				 "[ --pgdata --destroy ]",
				 "  --pgdata      path to data directory\n"
				 "  --destroy     also destroy Postgres database\n",
				 cli_drop_node_getopts,
				 cli_drop_monitor);

CommandLine drop_node_command =
	make_command(
		"node",
		"Drop a node from the pg_auto_failover monitor",
		"[ [ [ --pgdata ] [ --destroy ] ] | "
		"[ --monitor [ [ --hostname --pgport ] | [ --formation --name ] ] ] ] ",
		"  --pgdata      path to data directory\n"
		"  --monitor     pg_auto_failover Monitor Postgres URL\n"
		"  --formation   pg_auto_failover formation\n"
		"  --name        drop the node with the given node name\n"
		"  --hostname    drop the node with given hostname and pgport\n"
		"  --pgport      drop the node with given hostname and pgport\n"
		"  --destroy     also destroy Postgres database\n"
		"  --force       force dropping the node from the monitor\n"
		"  --wait        how many seconds to wait, default to 60 \n",
		cli_drop_node_getopts,
		cli_drop_node);

/*
 * cli_drop_node_getopts parses the command line options necessary to drop or
 * destroy a local pg_autoctl node.
 */
int
cli_drop_node_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "monitor", required_argument, NULL, 'm' },
		{ "destroy", no_argument, NULL, 'd' },
		{ "force", no_argument, NULL, 'F' },
		{ "hostname", required_argument, NULL, 'n' },
		{ "pgport", required_argument, NULL, 'p' },
		{ "formation", required_argument, NULL, 'f' },
		{ "wait", required_argument, NULL, 'w' },
		{ "name", required_argument, NULL, 'a' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	options.listen_notifications_timeout =
		PG_AUTOCTL_LISTEN_NOTIFICATIONS_TIMEOUT;

	while ((c = getopt_long(argc, argv, "D:dn:p:Vvqh",
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

			case 'm':
			{
				if (!validate_connection_string(optarg))
				{
					log_fatal("Failed to parse --monitor connection string, "
							  "see above for details.");
					exit(EXIT_CODE_BAD_ARGS);
				}
				strlcpy(options.monitor_pguri, optarg, MAXCONNINFO);
				log_trace("--monitor %s", options.monitor_pguri);
				break;
			}

			case 'd':
			{
				dropAndDestroy = true;
				log_trace("--destroy");
				break;
			}

			case 'F':
			{
				dropForce = true;
				log_trace("--force");
				break;
			}

			case 'n':
			{
				strlcpy(options.hostname, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--hostname %s", options.hostname);
				break;
			}

			case 'p':
			{
				if (!stringToInt(optarg, &options.pgSetup.pgport))
				{
					log_fatal("--pgport argument is a valid port number: \"%s\"",
							  optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
				log_trace("--pgport %d", options.pgSetup.pgport);
				break;
			}

			case 'f':
			{
				strlcpy(options.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", options.formation);
				break;
			}

			case 'a':
			{
				/* { "name", required_argument, NULL, 'a' }, */
				strlcpy(options.name, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--name %s", options.name);
				break;
			}

			case 'w':
			{
				/* { "wait", required_argument, NULL, 'w' }, */
				if (!stringToInt(optarg, &options.listen_notifications_timeout))
				{
					log_fatal("--wait argument is not a valid timeout: \"%s\"",
							  optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
				log_trace("--wait %d", options.listen_notifications_timeout);
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
				commandline_help(stderr);
				exit(EXIT_CODE_BAD_ARGS);
				break;
			}
		}
	}

	if (dropAndDestroy &&
		(!IS_EMPTY_STRING_BUFFER(options.hostname) ||
		 options.pgSetup.pgport != 0))
	{
		log_error("Please use either [ --hostname --pgport ] "
				  " or [ --formation --name ] to target a remote node, "
				  " or --destroy to destroy the local node.");
		log_info("Destroying a node is not supported from a distance");
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* now that we have the command line parameters, prepare the options */
	/* when we have a monitor URI we don't need PGDATA */
	if (cli_use_monitor_option(&options))
	{
		if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
		{
			log_warn("Given --monitor URI, the --pgdata option is ignored");
			log_info("Connecting to monitor at \"%s\"", options.monitor_pguri);

			/* the rest of the program needs pgdata actually empty */
			bzero((void *) options.pgSetup.pgdata,
				  sizeof(options.pgSetup.pgdata));
		}
	}
	else
	{
		(void) prepare_keeper_options(&options);
	}

	/*
	 * pg_autoctl drop node can be used with one of those set of arguments:
	 *   --pgdata ...                 # to drop the local node
	 *   --pgdata <monitor>           # to drop any node from the monitor
	 *   --formation ... --name ...   # address a node on the monitor
	 *   --hostname ... --pgport ...  # address a node on the monitor
	 *
	 * We check about the PGDATA being related to a monitor or a keeper later,
	 * here we focus on the optargs. Remember that --formation can be skipped
	 * to mean "default", and --pgport can be skipped to mean either PGPORT
	 * from the environment or just 5432.
	 */
	if (!IS_EMPTY_STRING_BUFFER(options.name) &&
		!IS_EMPTY_STRING_BUFFER(options.hostname))
	{
		log_fatal("pg_autoctl drop node target can either be specified "
				  "using [ --formation --name ], or "
				  "using [ --hostname and --pgport ], but not both.");
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* use the "default" formation when not given */
	if (IS_EMPTY_STRING_BUFFER(options.formation))
	{
		strlcpy(options.formation, FORMATION_DEFAULT, NAMEDATALEN);
	}

	/* publish our option parsing in the global variable */
	keeperOptions = options;

	return optind;
}


/*
 * cli_drop_node removes the local PostgreSQL node from the pg_auto_failover
 * monitor, and when it's a worker, from the Citus coordinator too.
 */
void
cli_drop_node(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;

	pgAutoCtlNodeRole localNodeRole =
		IS_EMPTY_STRING_BUFFER(config.pgSetup.pgdata)
		? PG_AUTOCTL_ROLE_UNKNOWN
		: ProbeConfigurationFileRole(config.pathnames.config);

	bool dropLocalNode =
		!IS_EMPTY_STRING_BUFFER(config.pgSetup.pgdata) &&
		localNodeRole == PG_AUTOCTL_ROLE_KEEPER;

	/*
	 * The configuration file is the last bit we remove, so we don't have to
	 * implement "continue from previous failed attempt" when the configuration
	 * file does not exist.
	 */
	if (dropLocalNode && !file_exists(config.pathnames.config))
	{
		log_error("Failed to find expected configuration file \"%s\"",
				  config.pathnames.config);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (dropLocalNode)
	{
		bool missingPgdataIsOk = true;
		bool pgIsNotRunningIsOk = true;
		bool monitorDisabledIsOk = true;

		if (!IS_EMPTY_STRING_BUFFER(config.hostname) ||
			config.pgSetup.pgport != 0)
		{
			log_fatal("Only dropping the local node is supported, "
					  "[ --hostname --pgport ] are not supported "
					  "when --pgdata is used.");
			log_info("To drop another node, please use this command "
					 "from the monitor itself.");
			exit(EXIT_CODE_BAD_ARGS);
		}

		if (!IS_EMPTY_STRING_BUFFER(config.name))
		{
			log_fatal("Only dropping the local node is supported, "
					  "[ --formation --name ] are not supported "
					  "when --pgdata is used.");
			log_info("To drop another node, please use this command "
					 "from the monitor itself.");
			exit(EXIT_CODE_BAD_ARGS);
		}

		/* just read the keeper file in given KeeperConfig */
		if (!keeper_config_read_file(&config,
									 missingPgdataIsOk,
									 pgIsNotRunningIsOk,
									 monitorDisabledIsOk))
		{
			exit(EXIT_CODE_BAD_CONFIG);
		}

		/* now drop the local node files, and maybe --destroy PGDATA */
		(void) cli_drop_local_node(&config, dropAndDestroy);

		return;
	}
	else
	{
		/* pg_autoctl drop node on the monitor drops another node */
		if (IS_EMPTY_STRING_BUFFER(config.name) &&
			IS_EMPTY_STRING_BUFFER(config.hostname))
		{
			log_fatal("pg_autoctl drop node target can either be specified "
					  "using [ --formation --name ], or "
					  "using [ --hostname and --pgport ], "
					  "please use either one.");
			exit(EXIT_CODE_BAD_ARGS);
		}

		(void) cli_drop_node_from_monitor_and_wait(&config);
	}
}


/*
 * cli_drop_monitor removes the local monitor node.
 */
static void
cli_drop_monitor(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;

	/*
	 * The configuration file is the last bit we remove, so we don't have to
	 * implement "continue from previous failed attempt" when the configuration
	 * file does not exist.
	 */
	if (!file_exists(config.pathnames.config))
	{
		log_error("Failed to find expected configuration file \"%s\"",
				  config.pathnames.config);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/*
	 * We are going to need to use the right pg_ctl binary to control the
	 * Postgres cluster: pg_ctl stop.
	 */
	switch (ProbeConfigurationFileRole(config.pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
			MonitorConfig mconfig = { 0 };

			if (!monitor_config_init_from_pgsetup(&mconfig,
												  &(config.pgSetup),
												  missingPgdataIsOk,
												  pgIsNotRunningIsOk))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			/* expose the pgSetup in the given KeeperConfig */
			config.pgSetup = mconfig.pgSetup;

			/* somehow at this point we've lost our pathnames */
			if (!keeper_config_set_pathnames_from_pgdata(
					&(config.pathnames),
					config.pgSetup.pgdata))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_ARGS);
			}

			/* drop the node and maybe destroy its PGDATA entirely. */
			(void) cli_drop_local_monitor(&mconfig, dropAndDestroy);
			return;
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			log_fatal("Local node is not a monitor");
			exit(EXIT_CODE_BAD_CONFIG);

			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  config.pathnames.config);
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}
}


/*
 * cli_drop_node_from_monitor calls pgautofailover.remove_node() on the monitor
 * for the given --hostname and --pgport, or from the given --formation and
 * --name.
 */
void
cli_drop_node_from_monitor(KeeperConfig *config, int64_t *nodeId, int *groupId)
{
	Monitor monitor = { 0 };

	(void) cli_monitor_init_from_option_or_config(&monitor, config);

	if (!IS_EMPTY_STRING_BUFFER(config->name))
	{
		log_info("Removing node with name \"%s\" in formation \"%s\" "
				 "from the monitor",
				 config->name, config->formation);

		if (!monitor_remove_by_nodename(&monitor,
										(char *) config->formation,
										(char *) config->name,
										dropForce,
										nodeId,
										groupId))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_MONITOR);
		}
	}
	else if (!IS_EMPTY_STRING_BUFFER(config->hostname))
	{
		int pgport =
			config->pgSetup.pgport > 0
			? config->pgSetup.pgport
			: pgsetup_get_pgport();

		log_info("Removing node with hostname \"%s\" and port %d "
				 "in formation \"%s\" from the monitor",
				 config->hostname, pgport, config->formation);

		if (!monitor_remove_by_hostname(&monitor,
										(char *) config->hostname,
										pgport,
										dropForce,
										nodeId,
										groupId))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_MONITOR);
		}
	}
	else
	{
		log_fatal("BUG: cli_drop_node_from_monitor options contain "
				  " neither --name nor --hostname");
		exit(EXIT_CODE_BAD_ARGS);
	}
}


/*
 * cli_drop_local_node drops the local node files, maybe including the PGDATA
 * directory (when --destroy has been used).
 */
void
cli_drop_local_node(KeeperConfig *config, bool dropAndDestroy)
{
	Keeper keeper = { 0 };
	Monitor *monitor = &(keeper.monitor);
	KeeperStateData *keeperState = &(keeper.state);

	keeper.config = *config;

	if (config->monitorDisabled)
	{
		(void) cli_drop_node_with_monitor_disabled(config, dropAndDestroy);

		/* make sure we're done now */
		exit(EXIT_CODE_QUIT);
	}

	(void) cli_monitor_init_from_option_or_config(monitor, config);

	/*
	 * First, read the state file and check that it has been assigned the
	 * DROPPED state already.
	 */
	if (!keeper_state_read(keeperState, config->pathnames.state))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_STATE);
	}

	/* first drop the node from the monitor  */
	if (keeperState->assigned_role != DROPPED_STATE)
	{
		int64_t nodeId = -1;
		int groupId = -1;

		(void) cli_drop_node_from_monitor(config, &nodeId, &groupId);
	}

	/*
	 * Now, when the pg_autoctl keeper service is still running, wait until
	 * it has reached the DROPPED/DROPPED state on-disk and then exited.
	 */
	pid_t pid = 0;

	/*
	 * Before continuing we need to make sure that a currently running service
	 * has stopped.
	 */
	bool stopped;
	if (dropForce)
	{
		/*
		 * If --force is used, we skip the transition to "dropped". So a
		 * currently running process won't realise it's dropped, which means it
		 * will not exit by itself. Thus all we need to know is if it's running
		 * now or not.
		 */
		if (!is_process_stopped(config->pathnames.pid, &stopped, &pid))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
	else
	{
		/*
		 * If --force isn't used then a running pg_autoctl process will detect
		 * that it is dropped and clean itself up nicely and finally it will
		 * exit. We give the process 30 seconds to exit by itself.
		 */
		if (!wait_for_process_to_stop(config->pathnames.pid, 30, &stopped, &pid))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}

	/*
	 * If the service is not stopped yet,  we just want to process to exit
	 * so we can take over. This can happen either because --force was used
	 * or because 30 seconds was not enough time for the service to exit.
	 */
	if (!stopped)
	{
		/* if the service isn't terminated, signal it to quit now */
		log_info("Sending signal %s to pg_autoctl process %d",
				 signal_to_string(SIGQUIT),
				 pid);

		if (kill(pid, SIGQUIT) != 0)
		{
			log_error("Failed to send SIGQUIT to the keeper's pid %d: %m", pid);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		if (!wait_for_process_to_stop(config->pathnames.pid, 30, &stopped, &pid) ||
			!stopped)
		{
			log_fatal("Failed to stop the pg_autoctl process with pid %d", pid);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}

	/*
	 * If the pg_autoctl keeper service was running at the beginning of this
	 * pg_autoctl drop node command, it should have reached the local DROPPED
	 * state already, and reported that to the monitor. But the process could
	 * have failed to communicate with the monitor, too.
	 *
	 * Also, if the pg_autoctl keeper service was not running, then we need to
	 * report that we've reached DROPPED state to the monitor now.
	 */
	bool dropped = false;

	if (keeper_ensure_node_has_been_dropped(&keeper, &dropped) && dropped)
	{
		log_info("This node with id %lld in formation \"%s\" and group %d "
				 "has been dropped from the monitor",
				 (long long) keeperState->current_node_id,
				 config->formation,
				 config->groupId);
	}
	else
	{
		log_fatal("Failed to ensure that the local node with id %lld "
				  "in formation \"%s\" and group %d has been removed "
				  "from the monitor",
				  (long long) keeperState->current_node_id,
				  config->formation,
				  config->groupId);
		exit(EXIT_CODE_MONITOR);
	}

	/*
	 * Either --destroy the whole Postgres cluster and configuration, or leave
	 * enough behind us that it's possible to re-join a formation later.
	 */
	if (dropAndDestroy)
	{
		(void) cli_drop_node_files_and_directories(config);
	}
	else
	{
		/*
		 * Now give the whole picture to the user, who might have missed our
		 * --destroy option and might want to use it now to start again with a
		 * fresh environment.
		 */
		log_warn("Preserving configuration file: \"%s\"",
				 config->pathnames.config);

		if (directory_exists(config->pgSetup.pgdata))
		{
			log_warn("Preserving Postgres Data Directory: \"%s\"",
					 config->pgSetup.pgdata);
		}

		log_info("pg_autoctl drop node keeps your data and setup safe, "
				 "you can still run Postgres or re-join a pg_auto_failover "
				 "cluster later");
		log_info("HINT: to completely remove your local Postgres instance and "
				 "setup, consider `pg_autoctl drop node --destroy`");
	}
}


/*
 * cli_drop_node_with_monitor_disabled implements pg_autoctl drop node for a
 * node that runs without a pg_auto_failover monitor.
 */
static void
cli_drop_node_with_monitor_disabled(KeeperConfig *config, bool dropAndDestroy)
{
	log_trace("cli_drop_node_with_monitor_disabled");

	if (dropAndDestroy)
	{
		pid_t pid = 0;

		/* first stop the pg_autoctl service if it's running */
		if (read_pidfile(config->pathnames.pid, &pid))
		{
			if (kill(pid, SIGQUIT) != 0)
			{
				log_error(
					"Failed to send SIGQUIT to the keeper's pid %d: %m",
					pid);
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			bool stopped;
			if (!wait_for_process_to_stop(config->pathnames.pid, 30, &stopped, &pid) ||
				!stopped)
			{
				log_fatal(
					"Failed to stop the pg_autoctl process with pid %d",
					pid);
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
		}

		(void) cli_drop_node_files_and_directories(config);
	}
	else
	{
		log_fatal("pg_autoctl drop node is not supported when "
				  "the monitor is disabled");
		log_info("Consider using the --destroy option");
		exit(EXIT_CODE_BAD_ARGS);
	}

	exit(EXIT_CODE_QUIT);
}


/*
 * cli_drop_node_files_and_directories removes the state files, configuration
 * files, and the PGDATA directory.
 */
static void
cli_drop_node_files_and_directories(KeeperConfig *config)
{
	/* Now remove the state files */
	if (!unlink_file(config->pathnames.init))
	{
		log_error("Failed to remove state init file \"%s\"",
				  config->pathnames.init);
	}

	if (!unlink_file(config->pathnames.state))
	{
		log_error("Failed to remove state file \"%s\"",
				  config->pathnames.state);
	}

	(void) stop_postgres_and_remove_pgdata_and_config(
		&config->pathnames,
		&config->pgSetup);
}


/*
 * cli_drop_local_monitor drops the local monitor files, maybe including the
 * PGDATA directory (when --destroy has been used).
 */
static void
cli_drop_local_monitor(MonitorConfig *mconfig, bool dropAndDestroy)
{
	/* stop the monitor service if it's still running */
	pid_t pid = 0;

	if (read_pidfile(mconfig->pathnames.pid, &pid))
	{
		if (kill(pid, SIGQUIT) != 0)
		{
			log_error("Failed to send SIGQUIT to the keeper's pid %d: %m", pid);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		bool stopped;
		if (!wait_for_process_to_stop(mconfig->pathnames.pid, 30, &stopped, &pid) ||
			!stopped)
		{
			log_fatal("Failed to stop the pg_autoctl process with pid %d", pid);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
	else
	{
		/* if we can't read a pidfile that exists on-disk, fail early */
		if (file_exists(mconfig->pathnames.pid))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_STATE);
		}
	}

	/*
	 * Either --destroy the whole Postgres cluster and configuration, or leave
	 * enough behind us that it's possible to re-join a formation later.
	 */
	if (dropAndDestroy)
	{
		if (!unlink_file(mconfig->pathnames.state))
		{
			log_error("Failed to remove state file \"%s\"",
					  mconfig->pathnames.state);
		}

		(void) stop_postgres_and_remove_pgdata_and_config(
			&mconfig->pathnames,
			&mconfig->pgSetup);
	}
	else
	{
		/*
		 * Now give the whole picture to the user, who might have missed our
		 * --destroy option and might want to use it now to start again with a
		 * fresh environment.
		 */
		log_warn("Preserving configuration file: \"%s\"",
				 mconfig->pathnames.config);

		if (directory_exists(mconfig->pgSetup.pgdata))
		{
			log_warn("Preserving Postgres Data Directory: \"%s\"",
					 mconfig->pgSetup.pgdata);
		}

		log_info("pg_autoctl drop node keeps your data and setup safe, "
				 "you can still run Postgres or re-join a pg_auto_failover "
				 "cluster later");
		log_info("HINT: to completely remove your local Postgres instance and "
				 "setup, consider `pg_autoctl drop node --destroy`");
	}
}


/*
 * stop_postgres_and_remove_pgdata_and_config stops PostgreSQL and then removes
 * PGDATA, and then config and state files.
 */
static void
stop_postgres_and_remove_pgdata_and_config(ConfigFilePaths *pathnames,
										   PostgresSetup *pgSetup)
{
	log_info("Stopping PostgreSQL at \"%s\"", pgSetup->pgdata);

	if (!pg_ctl_stop(pgSetup->pg_ctl, pgSetup->pgdata))
	{
		log_error("Failed to stop PostgreSQL at \"%s\"", pgSetup->pgdata);
		log_fatal("Skipping removal of directory \"%s\"", pgSetup->pgdata);
		exit(EXIT_CODE_PGCTL);
	}

	/*
	 * Only try to rm -rf PGDATA if we managed to stop PostgreSQL.
	 */
	if (directory_exists(pgSetup->pgdata))
	{
		log_info("Removing \"%s\"", pgSetup->pgdata);

		if (!rmtree(pgSetup->pgdata, true))
		{
			log_error("Failed to remove directory \"%s\": %m", pgSetup->pgdata);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
	else
	{
		log_warn("Skipping removal of \"%s\": directory does not exist",
				 pgSetup->pgdata);
	}

	log_info("Removing \"%s\"", pathnames->config);

	if (!unlink_file(pathnames->config))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_BAD_CONFIG);
	}
}


/*
 * cli_drop_node_from_monitor_and_wait waits until the node doesn't exist
 * anymore on the monitor, meaning it's been fully dropped now.
 */
static void
cli_drop_node_from_monitor_and_wait(KeeperConfig *config)
{
	bool dropped = false;
	Monitor monitor = { 0 };

	(void) cli_monitor_init_from_option_or_config(&monitor, config);

	/* call pgautofailover.remove_node() on the monitor */
	int64_t nodeId;
	int groupId;

	(void) cli_drop_node_from_monitor(config, &nodeId, &groupId);

	/* if the timeout is zero, just don't wait at all */
	if (config->listen_notifications_timeout == 0)
	{
		return;
	}

	log_info("Waiting until the node with id %lld in group %d has been "
			 "dropped from the monitor, or for %ds, whichever comes first",
			 (long long) nodeId, groupId, config->listen_notifications_timeout);

	uint64_t start = time(NULL);

	/* establish a connection for notifications if none present */
	(void) pgsql_prepare_to_wait(&(monitor.notificationClient));

	while (!dropped)
	{
		NodeAddressArray nodesArray = { 0 };

		bool groupStateHasChanged = false;
		int timeoutMs = PG_AUTOCTL_KEEPER_SLEEP_TIME * 1000;

		uint64_t now = time(NULL);

		if ((now - start) > config->listen_notifications_timeout)
		{
			log_error("Failed to wait until the node has been dropped");
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		(void) monitor_wait_for_state_change(&monitor,
											 config->formation,
											 groupId,
											 nodeId,
											 timeoutMs,
											 &groupStateHasChanged);

		if (!monitor_find_node_by_nodeid(&monitor,
										 config->formation,
										 groupId,
										 nodeId,
										 &nodesArray))
		{
			log_error("Failed to query monitor to see if node id %lld "
					  "has been dropped already",
					  (long long) nodeId);
			exit(EXIT_CODE_MONITOR);
		}

		dropped = nodesArray.count == 0;

		if (dropped)
		{
			log_info("Node with id %lld in group %d has been successfully "
					 "dropped from the monitor",
					 (long long) nodeId, groupId);
		}
	}
}
