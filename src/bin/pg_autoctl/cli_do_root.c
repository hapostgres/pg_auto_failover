/*
 * src/bin/pg_autoctl/cli_do_root.c
 *     Implementation of a CLI which lets you run operations on the local
 *     postgres server directly.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "cli_do_root.h"
#include "commandline.h"
#include "config.h"
#include "defaults.h"
#include "file_utils.h"
#include "fsm.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "monitor_config.h"
#include "pgctl.h"
#include "primary_standby.h"


CommandLine do_primary_adduser_monitor =
	make_command("monitor",
				 "add a local user for queries from the monitor",
				 "",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_create_monitor_user);

CommandLine do_primary_adduser_replica =
	make_command("replica",
				 "add a local user with replication privileges",
				 "",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_create_replication_user);

CommandLine *do_primary_adduser_subcommands[] = {
	&do_primary_adduser_monitor,
	&do_primary_adduser_replica,
	NULL
};

CommandLine do_primary_adduser =
	make_command_set("adduser",
					 "Create users on primary", NULL, NULL,
					 NULL, do_primary_adduser_subcommands);

CommandLine do_primary_slot_create =
	make_command("create",
				 "Create a replication slot on the primary server",
				 "",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_create_replication_slot);

CommandLine do_primary_slot_drop =
	make_command("drop",
				 "Drop a replication slot on the primary server",
				 "",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_drop_replication_slot);

CommandLine *do_primary_slot[] = {
	&do_primary_slot_create,
	&do_primary_slot_drop,
	NULL
};

CommandLine do_primary_slot_ =
	make_command_set("slot",
					 "Manage replication slot on the primary server", NULL, NULL,
					 NULL, do_primary_slot);

CommandLine do_primary_defaults =
	make_command("defaults",
				 "Add default settings to postgresql.conf",
				 "",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_add_default_settings);

CommandLine do_primary_identify_system =
	make_command("identify",
				 "Run the IDENTIFY_SYSTEM replication command on given host",
				 " host port",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_identify_system);

CommandLine *do_primary[] = {
	&do_primary_slot_,
	&do_primary_adduser,
	&do_primary_defaults,
	&do_primary_identify_system,
	NULL
};

CommandLine do_primary_ =
	make_command_set("primary",
					 "Manage a PostgreSQL primary server", NULL, NULL,
					 NULL, do_primary);

CommandLine do_standby_init =
	make_command("init",
				 "Initialize the standby server using pg_basebackup",
				 "[option ...] <primary name> <primary port> \n",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_init_standby);

CommandLine do_standby_rewind =
	make_command("rewind",
				 "Rewind a demoted primary server using pg_rewind",
				 "<primary host> <primary port>",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_rewind_old_primary);

CommandLine do_standby_crash_recovery =
	make_command("crash-recovery",
				 "Setup postgres for crash-recovery and start postgres",
				 " [ --pgdata ... ]",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_maybe_do_crash_recovery);

CommandLine do_standby_promote =
	make_command("promote",
				 "Promote a standby server to become writable",
				 "",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_promote_standby);

CommandLine *do_standby[] = {
	&do_standby_init,
	&do_standby_rewind,
	&do_standby_crash_recovery,
	&do_standby_promote,
	NULL
};

CommandLine do_standby_ =
	make_command_set("standby",
					 "Manage a PostgreSQL standby server", NULL, NULL,
					 NULL, do_standby);

CommandLine do_pgsetup_pg_ctl =
	make_command("pg_ctl",
				 "Find a non-ambiguous pg_ctl program and Postgres version",
				 "[option ...]",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_pgsetup_pg_ctl);

CommandLine do_pgsetup_discover =
	make_command("discover",
				 "Discover local PostgreSQL instance, if any",
				 "[option ...]",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_pgsetup_discover);

CommandLine do_pgsetup_is_ready =
	make_command("ready",
				 "Return true is the local Postgres server is ready",
				 "[option ...]",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_pgsetup_is_ready);

CommandLine do_pgsetup_wait_until_ready =
	make_command("wait",
				 "Wait until the local Postgres server is ready",
				 "[--read-write] [--timeout N] [option ...]",
				 "  --read-write    also wait until the server accepts read-write connections\n"
				 "  --timeout N     total timeout in seconds (default: 30)\n"
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_pgsetup_wait_getopts,
				 keeper_cli_pgsetup_wait_until_ready);

CommandLine do_pgsetup_startup_logs =
	make_command("logs",
				 "Outputs the Postgres startup logs",
				 "[option ...]",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_pgsetup_startup_logs);

CommandLine do_pgsetup_tune =
	make_command("tune",
				 "Compute and log some Postgres tuning options",
				 "[option ...]",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_pgsetup_tune);

CommandLine do_pgsetup_hba_lan =
	make_command("hba-lan",
				 "Append LAN CIDR trust rules to pg_hba.conf and reload Postgres",
				 "[option ...]",
				 KEEPER_CLI_WORKER_SETUP_OPTIONS,
				 keeper_cli_keeper_setup_getopts,
				 keeper_cli_pgsetup_hba_lan);

CommandLine *do_pgsetup[] = {
	&do_pgsetup_pg_ctl,
	&do_pgsetup_discover,
	&do_pgsetup_is_ready,
	&do_pgsetup_wait_until_ready,
	&do_pgsetup_startup_logs,
	&do_pgsetup_tune,
	&do_pgsetup_hba_lan,
	NULL
};

CommandLine do_pgsetup_commands =
	make_command_set("pgsetup",
					 "Manage a local Postgres setup", NULL, NULL,
					 NULL, do_pgsetup);

CommandLine do_tmux_compose_config =
	make_command("config",
				 "Produce a docker compose configuration file for a demo",
				 "[option ...]",
				 "  --root            path where to create a cluster\n"
				 "  --first-pgport    first Postgres port to use (5500)\n"
				 "  --nodes           number of Postgres nodes to create (2)\n"
				 "  --async-nodes     number of async nodes within nodes (0)\n"
				 "  --node-priorities list of nodes priorities (50)\n"
				 "  --sync-standbys   number-sync-standbys to set (0 or 1)\n"
				 "  --skip-pg-hba     use --skip-pg-hba when creating nodes\n"
				 "  --layout          tmux layout to use (even-vertical)\n"
				 "  --binpath         path to the pg_autoctl binary (current binary path)",
				 cli_do_tmux_script_getopts,
				 cli_do_tmux_compose_config);

CommandLine do_tmux_compose_script =
	make_command("script",
				 "Produce a tmux script for a demo or a test case (debug only)",
				 "[option ...]",
				 "  --root            path where to create a cluster\n"
				 "  --first-pgport    first Postgres port to use (5500)\n"
				 "  --nodes           number of Postgres nodes to create (2)\n"
				 "  --async-nodes     number of async nodes within nodes (0)\n"
				 "  --node-priorities list of nodes priorities (50)\n"
				 "  --sync-standbys   number-sync-standbys to set (0 or 1)\n"
				 "  --skip-pg-hba     use --skip-pg-hba when creating nodes\n"
				 "  --layout          tmux layout to use (even-vertical)\n"
				 "  --binpath         path to the pg_autoctl binary (current binary path)",
				 cli_do_tmux_script_getopts,
				 cli_do_tmux_compose_script);

CommandLine do_tmux_compose_session =
	make_command("session",
				 "Run a tmux session for a demo or a test case",
				 "[option ...]",
				 "  --root            path where to create a cluster\n"
				 "  --first-pgport    first Postgres port to use (5500)\n"
				 "  --nodes           number of Postgres nodes to create (2)\n"
				 "  --async-nodes     number of async nodes within nodes (0)\n"
				 "  --node-priorities list of nodes priorities (50)\n"
				 "  --sync-standbys   number-sync-standbys to set (0 or 1)\n"
				 "  --skip-pg-hba     use --skip-pg-hba when creating nodes\n"
				 "  --layout          tmux layout to use (even-vertical)\n"
				 "  --binpath         path to the pg_autoctl binary (current binary path)",
				 cli_do_tmux_script_getopts,
				 cli_do_tmux_compose_session);

CommandLine *do_tmux_compose[] = {
	&do_tmux_compose_config,
	&do_tmux_compose_script,
	&do_tmux_compose_session,
	NULL
};

CommandLine do_tmux_compose_commands =
	make_command_set("compose",
					 "Set of facilities to handle docker compose sessions",
					 NULL, NULL, NULL, do_tmux_compose);

CommandLine do_tmux_script =
	make_command("script",
				 "Produce a tmux script for a demo or a test case (debug only)",
				 "[option ...]",
				 "  --root              path where to create a cluster\n"
				 "  --first-pgport      first Postgres port to use (5500)\n"
				 "  --nodes             number of Postgres nodes to create (2)\n"
				 "  --async-nodes       number of async nodes within nodes (0)\n"
				 "  --node-priorities   list of nodes priorities (50)\n"
				 "  --sync-standbys     number-sync-standbys to set (0 or 1)\n"
				 "  --skip-pg-hba       use --skip-pg-hba when creating nodes\n"
				 "  --citus             start a Citus formation\n"
				 "  --citus-workers     number of Citus workers to create (2)\n"
				 "  --citus-secondaries number of Citus secondaries to create (0)\n"
				 "  --layout            tmux layout to use (even-vertical)\n"
				 "  --binpath           path to the pg_autoctl binary (current binary path)",
				 cli_do_tmux_script_getopts,
				 cli_do_tmux_script);

CommandLine do_tmux_session =
	make_command("session",
				 "Run a tmux session for a demo or a test case",
				 "[option ...]",
				 "  --root              path where to create a cluster\n"
				 "  --first-pgport      first Postgres port to use (5500)\n"
				 "  --nodes             number of Postgres nodes to create (2)\n"
				 "  --async-nodes       number of async nodes within nodes (0)\n"
				 "  --node-priorities   list of nodes priorities (50)\n"
				 "  --sync-standbys     number-sync-standbys to set (0 or 1)\n"
				 "  --skip-pg-hba       use --skip-pg-hba when creating nodes\n"
				 "  --citus             start a Citus formation\n"
				 "  --citus-workers     number of Citus workers to create (2)\n"
				 "  --citus-secondaries number of Citus secondaries to create (0)\n"
				 "  --layout            tmux layout to use (even-vertical)\n"
				 "  --binpath           path to the pg_autoctl binary (current binary path)",
				 cli_do_tmux_script_getopts,
				 cli_do_tmux_session);


CommandLine do_tmux_stop =
	make_command("stop",
				 "Stop pg_autoctl processes that belong to a tmux session ",
				 "[option ...]",
				 "  --root          path where to create a cluster\n"
				 "  --first-pgport  first Postgres port to use (5500)\n"
				 "  --nodes         number of Postgres nodes to stop\n"
				 "  --citus         stop a Citus formation\n"
				 "  --citus-workers number of Citus workers to stop\n",
				 cli_do_tmux_script_getopts,
				 cli_do_tmux_stop);

CommandLine do_tmux_clean =
	make_command("clean",
				 "Clean-up a tmux session processes and root dir",
				 "[option ...]",
				 "  --root          path where to create a cluster\n"
				 "  --first-pgport  first Postgres port to use (5500)\n"
				 "  --nodes         number of Postgres nodes to clean\n"
				 "  --citus         clean a Citus formation\n"
				 "  --citus-workers number of Citus workers to clean\n",
				 cli_do_tmux_script_getopts,
				 cli_do_tmux_clean);

CommandLine do_tmux_wait =
	make_command("wait",
				 "Wait until a given node has been registered on the monitor",
				 "[option ...] nodename [ targetState ]",
				 "  --root            path where to create a cluster\n"
				 "  --first-pgport    first Postgres port to use (5500)\n"
				 "  --nodes           number of Postgres nodes to create (2)\n"
				 "  --async-nodes     number of async nodes within nodes (0)\n"
				 "  --node-priorities list of nodes priorities (50)\n"
				 "  --sync-standbys   number-sync-standbys to set (0 or 1)\n"
				 "  --skip-pg-hba     use --skip-pg-hba when creating nodes\n"
				 "  --layout          tmux layout to use (even-vertical)",
				 cli_do_tmux_script_getopts,
				 cli_do_tmux_wait);

CommandLine *do_tmux[] = {
	&do_tmux_compose_commands,
	&do_tmux_script,
	&do_tmux_session,
	&do_tmux_stop,
	&do_tmux_wait,
	&do_tmux_clean,
	NULL
};

CommandLine do_tmux_commands =
	make_command_set("tmux",
					 "Set of facilities to handle tmux interactive sessions",
					 NULL, NULL, NULL, do_tmux);


/*
 * pg_autoctl internal service postgres|listener|node-active
 *
 * These are the subprocess entry points used by the supervisor (pg_autoctl run
 * and pg_autoctl create … --run).  The supervisor fork()s and then execv()s
 * the pg_autoctl binary itself with one of these sub-commands so that each
 * service runs in its own address space.
 *
 * Using fork+exec (rather than fork alone) is a deliberate design choice for
 * live upgrades: when a child process exits with an incompatible monitor
 * extension version, the supervisor restarts it via fork()+execv(), which loads
 * the current binary from disk.  If the binary has been updated in place (e.g.
 * by a package manager), the restarted child automatically picks up the new
 * version without touching the supervisor process — making pg_autoctl safe to
 * use as PID 1 in Docker/Kubernetes containers where replacing the binary and
 * sending SIGTERM would lose the container.
 *
 * See also: keeper.c keeper_check_monitor_extension_version(), which exits on
 * version mismatch precisely to trigger this restart-with-new-binary path.
 *
 * These commands are hidden from --help output (make_hidden_command_set) so
 * operators do not accidentally invoke them directly.
 * Use "pg_autoctl manual service" for the user-facing controls (restart, pgctl).
 * Use "pg_autoctl inspect getpid" to read sub-process PIDs.
 */
static CommandLine *internal_service_subcommands[] = {
	&service_pgcontroller,      /* debug: supervisor for just the postgres controller */
	&service_postgres,          /* spawned by service_postgres_ctl_start() */
	&service_monitor_listener,  /* spawned by service_monitor_start() */
	&service_node_active,       /* spawned by service_keeper_start() */
	NULL
};

static CommandLine internal_service_commands =
	make_hidden_command_set("service",
							"Subprocess entry points for the pg_autoctl supervisor",
							NULL, NULL, NULL, internal_service_subcommands);

/*
 * pg_autoctl internal
 *
 * Hidden from --help; routable so the supervisor's execv() calls work.
 * Contains only what the supervisor spawns plus dev/QA tooling not yet
 * moved to pgaftest (tmux, demo).
 */
CommandLine *do_subcommands[] = {
	&internal_service_commands,
	&do_tmux_commands,
	&do_demo_commands,
	NULL
};

CommandLine internal_commands =
	make_hidden_command_set("internal",
							"Internal subprocess entry points — not for direct use",
							NULL, NULL, NULL, do_subcommands);

/*
 * pg_autoctl do  — v2.2 compatibility alias for "internal".
 *
 * Before v2.3 (commit c32eb03) the supervisor spawned service subprocesses
 * via "pg_autoctl do service {postgres,listener,node-active}".  The command
 * was renamed to "pg_autoctl internal service X" in v2.3.  During an in-place
 * upgrade the v2.2 supervisor (still running as PID 1) re-execs after the
 * symlink flip, so the v2.3 binary must still accept the old "do service X"
 * form.  Routing to the same do_subcommands[] array ensures the exact same
 * callbacks fire regardless of which prefix is used.
 */
CommandLine do_compat_commands =
	make_hidden_command_set("do",
							"v2.2 compat alias for 'internal' — not for direct use",
							NULL, NULL, NULL, do_subcommands);


/*
 * keeper_cli_keeper_setup_getopts parses command line options and set the
 * global variable keeperOptions from them, without doing any check.
 */
int
keeper_cli_keeper_setup_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };

	SSLCommandLineOptions sslCommandLineOptions = SSL_CLI_UNKNOWN;

	static struct option long_options[] = {
		{ "pgctl", required_argument, NULL, 'C' },
		{ "pgdata", required_argument, NULL, 'D' },
		{ "pghost", required_argument, NULL, 'H' },
		{ "pgport", required_argument, NULL, 'p' },
		{ "listen", required_argument, NULL, 'l' },
		{ "username", required_argument, NULL, 'U' },
		{ "auth", required_argument, NULL, 'A' },
		{ "skip-pg-hba", no_argument, NULL, 'S' },
		{ "dbname", required_argument, NULL, 'd' },
		{ "hostname", required_argument, NULL, 'n' },
		{ "formation", required_argument, NULL, 'f' },
		{ "monitor", required_argument, NULL, 'm' },
		{ "disable-monitor", no_argument, NULL, 'M' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ "candidate-priority", required_argument, NULL, 'P' },
		{ "replication-quorum", required_argument, NULL, 'r' },
		{ "run", no_argument, NULL, 'x' },
		{ "help", no_argument, NULL, 0 },
		{ "no-ssl", no_argument, NULL, 'N' },
		{ "ssl-self-signed", no_argument, NULL, 's' },
		{ "ssl-mode", required_argument, &ssl_flag, SSL_MODE_FLAG },
		{ "ssl-ca-file", required_argument, &ssl_flag, SSL_CA_FILE_FLAG },
		{ "ssl-crl-file", required_argument, &ssl_flag, SSL_CRL_FILE_FLAG },
		{ "server-cert", required_argument, &ssl_flag, SSL_SERVER_CRT_FLAG },
		{ "server-key", required_argument, &ssl_flag, SSL_SERVER_KEY_FLAG },
		{ NULL, 0, NULL, 0 }
	};

	/*
	 * The only command lines that are using keeper_cli_getopt_pgdata are
	 * terminal ones: they don't accept subcommands. In that case our option
	 * parsing can happen in any order and we don't need getopt_long to behave
	 * in a POSIXLY_CORRECT way.
	 *
	 * The unsetenv() call allows getopt_long() to reorder arguments for us.
	 */
	unsetenv("POSIXLY_CORRECT");

	int optind = cli_common_keeper_getopts(argc, argv,
										   long_options,
										   "C:D:H:p:l:U:A:SLd:n:f:m:MRVvqhP:r:xsN",
										   &options,
										   &sslCommandLineOptions);

	/* publish our option parsing in the global variable */
	keeperOptions = options;

	return optind;
}
