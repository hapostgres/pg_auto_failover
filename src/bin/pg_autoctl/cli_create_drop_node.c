/*
 * src/bin/pg_autoctl/cli_create_drop_node.c
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
#include "primary_standby.h"

MonitorConfig monitorOptions;

static int cli_create_postgres_getopts(int argc, char **argv);
static void cli_create_postgres(int argc, char **argv);

static int cli_create_monitor_getopts(int argc, char **argv);
static void cli_create_monitor(int argc, char **argv);

static void cli_drop_node(int argc, char **argv);

static bool discover_nodename(char *nodename, int size,
							  const char *monitorHostname, int monitorPort);
static void check_nodename(const char *nodename);


CommandLine create_monitor_command =
	make_command("monitor",
				 "Initialize a pg_auto_failover monitor node",
				 " [ --pgdata --pgport --pgctl --nodename ] ",
				 "  --pgctl       path to pg_ctl\n" \
				 "  --pgdata      path to data directory\n" \
				 "  --pgport      PostgreSQL's port number\n" \
				 "  --nodename    hostname by which postgres is reachable\n" \
				 "  --auth        authentication method for connections from data nodes\n",
				 cli_create_monitor_getopts,
				 cli_create_monitor);

CommandLine create_postgres_command =
	make_command("postgres",
				 "Initialize a pg_auto_failover standalone postgres node",
				 "",
				 "  --pgctl       path to pg_ctl\n"
				 "  --pgdata      path to data director\n"
				 "  --pghost      PostgreSQL's hostname\n"
				 "  --pgport      PostgreSQL's port number\n"
				 "  --listen      PostgreSQL's listen_addresses\n"
				 "  --username    PostgreSQL's username\n"
				 "  --dbname      PostgreSQL's database name\n"
				 "  --nodename    pg_auto_failover node\n"
				 "  --formation   pg_auto_failover formation\n"
				 "  --monitor     pg_auto_failover Monitor Postgres URL\n"
				 "  --auth        authentication method for connections from monitor\n"
				 KEEPER_CLI_ALLOW_RM_PGDATA_OPTION,
				 cli_create_postgres_getopts,
				 cli_create_postgres);

CommandLine drop_node_command =
	make_command("node",
				 "Drop a node from the pg_auto_failover monitor",
				 " [ --pgdata ]",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 cli_drop_node);


/*
 * cli_create_config manages the whole set of configuration parameters that
 * pg_autoctl accepts and deals with either creating a configuration file if
 * necessary, or merges the command line arguments into the pre-existing
 * configuration file.
 */
bool
cli_create_config(Keeper *keeper, KeeperConfig *config)
{
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;

	/*
	 * We support two modes of operations here:
	 *   - configuration exists already, we need PGDATA
	 *   - configuration doesn't exist already, we need PGDATA, and more
	 */
	if (!keeper_config_set_pathnames_from_pgdata(&config->pathnames,
												 config->pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (file_exists(config->pathnames.config))
	{
		KeeperConfig options = *config;

		if (!keeper_config_read_file(config,
									 missing_pgdata_is_ok,
									 pg_is_not_running_is_ok))
		{
			log_fatal("Failed to read configuration file \"%s\"",
					  config->pathnames.config);
			exit(EXIT_CODE_BAD_CONFIG);
		}

		/*
		 * Now that we have loaded the configuration file, apply the command
		 * line options on top of it, giving them priority over the config.
		 */
		if (!keeper_config_merge_options(config, &options))
		{
			/* errors have been logged already */
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}
	else
	{
		/* set our KeeperConfig from the command line options now. */
		keeper_config_init(config,
						   missing_pgdata_is_ok, pg_is_not_running_is_ok);

		/* and write our brand new setup to file */
		if (!keeper_config_write_file(config))
		{
			log_fatal("Failed to write the pg_autoctl configuration file, "
					  "see above");
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}

	return true;
}


/*
 * cli_pg_create calls keeper_pg_init and handle errors and warnings, then
 * destroys the extra config structure instance from the command line option
 * handling.
 */
void
cli_create_pg(Keeper *keeper, KeeperConfig *config)
{
	if (!keeper_pg_init(keeper, config))
	{
		/* errors have been logged */
		exit(EXIT_CODE_BAD_STATE);
	}

	if (keeperInitWarnings)
	{
		log_info("Keeper has been succesfully initialized, "
				 "please fix above warnings to complete installation.");
	}
	else
	{
		log_info("Keeper has been succesfully initialized.");
	}

	keeper_config_destroy(config);
}


/*
 * cli_create_postgres_getopts parses command line options and set the global
 * variable keeperOptions from them, without doing any check.
 */
static int
cli_create_postgres_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };

	static struct option long_options[] = {
		{ "pgctl", required_argument, NULL, 'C' },
		{ "pgdata", required_argument, NULL, 'D' },
		{ "pghost", required_argument, NULL, 'h' },
		{ "pgport", required_argument, NULL, 'p' },
		{ "listen", required_argument, NULL, 'l' },
		{ "username", required_argument, NULL, 'U' },
		{ "auth", required_argument, NULL, 'A' },
		{ "dbname", required_argument, NULL, 'd' },
		{ "nodename", required_argument, NULL, 'n' },
		{ "formation", required_argument, NULL, 'f' },
		{ "monitor", required_argument, NULL, 'm' },
		{ "allow-removing-pgdata", no_argument, NULL, 'R' },
		{ "help", no_argument, NULL, 0 },
		{ NULL, 0, NULL, 0 }
	};

	int optind =
		cli_create_node_getopts(argc, argv,
								long_options, "C:D:h:p:l:U:A:d:n:f:m:R",
								&options);

	/* publish our option parsing in the global variable */
	keeperOptions = options;

	return optind;
}


/*
 * cli_create_postgres prepares a local PostgreSQL instance to be used as a
 * standalone Postgres instance, not in a Citus formation.
 */
static void
cli_create_postgres(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;

	/* pg_autoctl create postgres: mark ourselves as a standalone node */
	config.pgSetup.pgKind = NODE_KIND_STANDALONE;
	strlcpy(config.nodeKind, "standalone", NAMEDATALEN);

	if (!check_or_discover_nodename(&config))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!cli_create_config(&keeper, &config))
	{
		log_error("Failed to initialize our configuration, see above.");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	cli_create_pg(&keeper, &config);
}


/*
 * cli_create_monitor_getopts parses the command line options necessary to
 * initialise a PostgreSQL instance as our monitor.
 */
static int
cli_create_monitor_getopts(int argc, char **argv)
{
	MonitorConfig options = { 0 };
	int c, option_index, errors = 0;

	static struct option long_options[] = {
		{ "pgctl", required_argument, NULL, 'C' },
		{ "pgdata", required_argument, NULL, 'D' },
		{ "pgport", required_argument, NULL, 'p' },
		{ "nodename", required_argument, NULL, 'n' },
		{ "listen", required_argument, NULL, 'l' },
		{ "auth", required_argument, NULL, 'A' },
		{ "help", no_argument, NULL, 0 },
		{ NULL, 0, NULL, 0 }
	};

	/* hard-coded defaults */
	options.pgSetup.pgport = 5432;

	optind = 0;

	while ((c = getopt_long(argc, argv, "C:D:p:A:",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'C':
			{
				strlcpy(options.pgSetup.pg_ctl, optarg, MAXPGPATH);
				log_trace("--pg_ctl %s", options.pgSetup.pg_ctl);
				break;
			}

			case 'D':
			{
				strlcpy(options.pgSetup.pgdata, optarg, MAXPGPATH);
				log_trace("--pgdata %s", options.pgSetup.pgdata);
				break;
			}

			case 'p':
			{
				int scanResult = sscanf(optarg, "%d", &(options.pgSetup.pgport));
				if (scanResult == 0)
				{
					log_fatal("--pgport argument is a valid port number: \"%s\"",
							  optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
				log_trace("--pgport %d", options.pgSetup.pgport);
				break;
			}

			case 'l':
			{
				strlcpy(options.pgSetup.listen_addresses, optarg, MAXPGPATH);
				log_trace("--listen %s", options.pgSetup.listen_addresses);
				break;
			}

			case 'n':
			{
				strlcpy(options.nodename, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--nodename %s", options.nodename);
				break;
			}

			case 'A':
			{
				strlcpy(options.pgSetup.authMethod, optarg, NAMEDATALEN);
				log_trace("--auth %s", options.pgSetup.authMethod);
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

	/*
	 * We're not using pg_setup_init() here: we are following a very different
	 * set of rules. We just want to check:
	 *
	 *   - PGDATA is set and the directory does not exists
	 *   - PGPORT is either set or defaults to 5432
	 *
	 * Also we use the first pg_ctl binary found in the PATH, we're not picky
	 * here, we don't have to manage the whole life-time of that PostgreSQL
	 * instance.
	 */
	if (IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
	{
		char *pgdata = getenv("PGDATA");

		if (pgdata == NULL)
		{
			log_fatal("Failed to set PGDATA either from the environment "
					  "or from --pgdata");
			exit(EXIT_CODE_BAD_ARGS);
		}

		strlcpy(options.pgSetup.pgdata, pgdata, MAXPGPATH);
	}

	if (IS_EMPTY_STRING_BUFFER(options.pgSetup.pg_ctl))
	{
		set_first_pgctl(&(options.pgSetup));
	}

	if (IS_EMPTY_STRING_BUFFER(options.pgSetup.listen_addresses))
	{
		strlcpy(options.pgSetup.listen_addresses,
				POSTGRES_DEFAULT_LISTEN_ADDRESSES, MAXPGPATH);
	}

	/* publish our option parsing in the global variable */
	monitorOptions = options;

	return optind;
}


/*
 * Initialize the PostgreSQL instance that we're using for the Monitor:
 *
 *  - pg_ctl initdb
 *  - add postgresql-citus.conf to postgresql.conf
 *  - pg_ctl start
 *  - create user autoctl with createdb login;
 *  - create database pg_auto_failover with owner autoctl;
 *  - create extension pgautofailover;
 *
 * When this function is called (from monitor_config_init at the CLI level), we
 * know that PGDATA has been initdb already, and that's about it.
 *
 */
static void
cli_create_monitor(int argc, char **argv)
{
	Monitor monitor = { 0 };
	MonitorConfig config = monitorOptions;
	char connInfo[MAXCONNINFO];
	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;

	/*
	 * We support two modes of operations here:
	 *   - configuration exists already, we need PGDATA
	 *   - configuration doesn't exist already, we need PGDATA, and more
	 */
	if (!monitor_config_set_pathnames_from_pgdata(&config))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (file_exists(config.pathnames.config))
	{
		MonitorConfig options = config;

		if (!monitor_config_read_file(&config,
									  missingPgdataIsOk, pgIsNotRunningIsOk))
		{
			log_fatal("Failed to read configuration file \"%s\"",
					  config.pathnames.config);
			exit(EXIT_CODE_BAD_CONFIG);
		}

		/*
		 * Now that we have loaded the configuration file, apply the command
		 * line options on top of it, giving them priority over the config.
		 */
		if (!monitor_config_merge_options(&config, &options))
		{
			/* errors have been logged already */
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}
	else
	{
		/* Take care of the --nodename */
		if (IS_EMPTY_STRING_BUFFER(config.nodename))
		{
			if (!discover_nodename((char *) (&config.nodename),
								   _POSIX_HOST_NAME_MAX ,
								   DEFAULT_INTERFACE_LOOKUP_SERVICE_NAME,
								   DEFAULT_INTERFACE_LOOKUP_SERVICE_PORT))
			{
				log_fatal("Failed to auto-detect the hostname of this machine, "
						  "please provide one via --nodename");
				exit(EXIT_CODE_BAD_ARGS);
			}
		}
		else
		{
			/*
			 * When provided with a --nodename option, we run some checks on
			 * the user provided value based on Postgres usage for the hostname
			 * in its HBA setup. Both forward and reverse DNS needs to return
			 * meaningful values for the connections to be granted when using a
			 * hostname.
			 *
			 * That said network setup is something complex and we don't
			 * pretend we are able to avoid any and all false negatives in our
			 * checks, so we only WARN when finding something that might be
			 * fishy, and proceed with the setup of the local node anyway.
			 */
			(void) check_nodename(config.nodename);
		}

		/* set our MonitorConfig from the command line options now. */
		monitor_config_init(&config,
							missingPgdataIsOk, pgIsNotRunningIsOk);

		/* and write our brand new setup to file */
		if (!monitor_config_write_file(&config))
		{
			log_fatal("Failed to write the monitor's configuration file, "
					  "see above");
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}

	pg_setup_get_local_connection_string(&(config.pgSetup), connInfo);
	monitor_init(&monitor, connInfo);

	/* Ok, now we know we have a configuration file, and it's been loaded. */
	if (!monitor_pg_init(&monitor, &config))
	{
		/* errors have been logged */
		exit(EXIT_CODE_BAD_STATE);
	}

	log_info("Monitor has been succesfully initialized.");
}


/*
 * cli_drop_node removes the local PostgreSQL node from the pg_auto_failover
 * monitor, and when it's a worker, from the Citus coordinator too.
 */
static void
cli_drop_node(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;
	pid_t pid;

	if (!keeper_config_read_file(&config,
								 missing_pgdata_is_ok,
								 pg_is_not_running_is_ok))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_remove(&keeper, &config))
	{
		log_fatal("Failed to remove local node from the pg_auto_failover monitor, "
				  "see above for details");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	log_info("Removed the keeper from the monitor and removed the state file "
			 "from disk.");

	/*
	 * Now also stop Postgres and the keeper itself, keeper first.
	 *
	 * We need to stop Postgres because otherwise we won't be able to drop the
	 * replication slot on the other node, because it's still active.
	 */
	if (read_pidfile(keeper.config.pathnames.pid, &pid))
	{
		if (kill(pid, SIGTERM) != 0)
		{
			log_error("Failed to send %s to the keeper's pid %d: %s",
					  strsignal(SIGTERM), pid, strerror(errno));
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
		else
		{
			log_info("Stopped the pg_autoctl service.");
		}
	}

	if (pg_ctl_stop(config.pgSetup.pg_ctl, config.pgSetup.pgdata))
	{
		log_info("Stopped PostgreSQL instance at \"%s\"", config.pgSetup.pgdata);
	}
	else
	{
		log_error("Failed  to stop PostgreSQL at \"%s\"", config.pgSetup.pgdata);
		exit(EXIT_CODE_PGCTL);
	}

	keeper_config_destroy(&config);
}


/*
 * check_or_discover_nodename checks given --nodename or attempt to discover a
 * suitable default value for the current node when it's not been provided on
 * the command line.
 */
bool
check_or_discover_nodename(KeeperConfig *config)
{
	/* take care of the nodename */
	if (IS_EMPTY_STRING_BUFFER(config->nodename))
	{
		char monitorHostname[_POSIX_HOST_NAME_MAX];
		int monitorPort = 0;

		if (!hostname_from_uri(config->monitor_pguri,
							   monitorHostname, _POSIX_HOST_NAME_MAX,
							   &monitorPort))
		{
			log_fatal("Failed to determine monitor hostname when parsing "
					  "Postgres URI \"%s\"", config->monitor_pguri);
			return false;
		}

		if (!discover_nodename((char *) &(config->nodename),
							   _POSIX_HOST_NAME_MAX,
							   monitorHostname,
							   monitorPort))
		{
			log_fatal("Failed to auto-detect the hostname of this machine, "
					  "please provide one via --nodename");
			return false;
		}
	}
	else
	{
		/*
		 * When provided with a --nodename option, we run some checks on the
		 * user provided value based on Postgres usage for the hostname in its
		 * HBA setup. Both forward and reverse DNS needs to return meaningful
		 * values for the connections to be granted when using a hostname.
		 *
		 * That said network setup is something complex and we don't pretend we
		 * are able to avoid any and all false negatives in our checks, so we
		 * only WARN when finding something that might be fishy, and proceed
		 * with the setup of the local node anyway.
		 */
		(void) check_nodename(config->nodename);
	}
	return true;
}


/*
 * discover_nodename discovers a suitable --nodename default value in three
 * steps:
 *
 * 1. First find the local LAN IP address by connecting a socket() to either an
 *    internet service (8.8.8.8:53) or to the monitor's hostname and port, and
 *    then inspecting which local address has been used.
 *
 * 2. Use the local IP address obtained in the first step and do a reverse DNS
 *    lookup for it. The answer is our candidate default --nodename.
 *
 * 3. Do a DNS lookup for the candidate default --nodename. If we get back a IP
 *    address that matches one of the local network interfaces, we keep the
 *    candidate, the DNS lookup that Postgres does at connection time is
 *    expected to then work.
 *
 * All this dansing around DNS lookups is necessary in order to mimic Postgres
 * HBA matching of hostname rules against client IP addresses: the hostname in
 * the HBA rule is resolved and compared to the client IP address. We want the
 * --nodename we use to resolve to an IP address that exists on the local
 * Postgres server.
 *
 * Worst case here is that we fail to discover a --nodename and then ask the
 * user to provide one for us.
 *
 * monitorHostname and monitorPort are used to open a socket to that address,
 * in order to find the right outbound interface. When creating a monitor node,
 * of course, we don't have the monitorHostname yet: we are trying to discover
 * it... in that case we use PG_AUTOCTL_DEFAULT_SERVICE_NAME and PORT, which
 * are the Google DNS service: 8.8.8.8:53, expected to be reachable.
 */
static bool
discover_nodename(char *nodename, int size,
				  const char *monitorHostname, int monitorPort)
{
	/*
	 * Try and find a default --nodename. The --nodename is mandatory, so
	 * when not provided for by the user, then failure to discover a
	 * suitable nodename is a fatal error.
	 */
	char ipAddr[BUFSIZE];
	char localIpAddr[BUFSIZE];
	char hostname[_POSIX_HOST_NAME_MAX];

	/* fetch our local address among the network interfaces */
	if (!fetchLocalIPAddress(ipAddr, BUFSIZE, monitorHostname, monitorPort))
	{
		log_fatal("Failed to find a local IP address, "
				  "please provide --nodename.");
		return false;
	}

	/* from there on we can take the ipAddr as the default --nodename */
	strlcpy(nodename, ipAddr, size);
	log_debug("discover_nodename: local ip %s", ipAddr);

	/* do a reverse DNS lookup from our local LAN ip address */
	if (!findHostnameFromLocalIpAddress(ipAddr,
										hostname, _POSIX_HOST_NAME_MAX))
	{
		/* errors have already been logged */
		log_info("Using local IP address \"%s\" as the --nodename.", ipAddr);
		return true;
	}
	log_debug("discover_nodename: host from ip %s", hostname);

	/* do a DNS lookup of the hostname we got from the IP address */
	if (!findHostnameLocalAddress(hostname, localIpAddr, BUFSIZE))
	{
		/* errors have already been logged */
		log_info("Using local IP address \"%s\" as the --nodename.", ipAddr);
		return true;
	}
	log_debug("discover_nodename: ip from host %s", localIpAddr);

	/*
	 * ok ipAddr resolves to an hostname that resolved back to a local address,
	 * we should be able to use the hostname in pg_hba.conf
	 */
	strlcpy(nodename, hostname, size);
	log_info("Using --nodename \"%s\", which resolves to IP address \"%s\"",
			 nodename, localIpAddr);

	return true;
}


/*
 * check_nodename runs some DNS check against the provided --nodename in order
 * to warn the user in case we might later fail to use it in the Postgres HBA
 * setup.
 *
 * The main trouble we guard against is from HBA authentication. Postgres HBA
 * check_hostname() does a DNS lookup of the hostname found in the pg_hba.conf
 * file and then compares the IP addresses obtained to the client IP address,
 * and refuses the connection where there's no match.
 */
static void
check_nodename(const char *nodename)
{
	char localIpAddress[INET_ADDRSTRLEN];
	IPType ipType = ip_address_type(nodename);

	if (ipType == IPTYPE_NONE)
	{
		if (!findHostnameLocalAddress(nodename,
									  localIpAddress, INET_ADDRSTRLEN))
		{
			log_warn(
				"Failed to resolve nodename \"%s\" to a local IP address, "
				"automated pg_hba.conf setup might fail.", nodename);
		}
	}
	else
	{
		char cidr[BUFSIZE];

		if (!fetchLocalCIDR(nodename, cidr, BUFSIZE))
		{
			log_warn("Failed to find adress \"%s\" in local network "
					 "interfaces, automated pg_hba.conf setup might fail.",
					 nodename);
		}
	}
}
