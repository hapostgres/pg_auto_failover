/*
 * src/bin/pg_autoctl/cli_common.h
 *     Implementation of a CLI which lets you run individual keeper routines
 *     directly
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef CLI_COMMON_H
#define CLI_COMMON_H

#include <getopt.h>

#include "keeper.h"
#include "keeper_config.h"
#include "monitor.h"
#include "monitor_config.h"

extern MonitorConfig monitorOptions;
extern KeeperConfig keeperOptions;
extern bool allowRemovingPgdata;

#define KEEPER_CLI_WORKER_SETUP_OPTIONS \
	"  --pgctl       path to pg_ctl\n" \
	"  --pgdata      path to data director\n" \
	"  --pghost      PostgreSQL's hostname\n" \
	"  --pgport      PostgreSQL's port number\n" \
	"  --listen      PostgreSQL's listen_addresses\n" \
	"  --username    PostgreSQL's username\n" \
	"  --dbname      PostgreSQL's database name\n" \
	"  --proxyport   Proxy's port number\n" \
	"  --nodename    pg_auto_failover node\n" \
	"  --formation   pg_auto_failover formation\n" \
	"  --group       pg_auto_failover group Id\n" \
	"  --monitor     pg_auto_failover Monitor Postgres URL\n" \

#define KEEPER_CLI_NON_WORKER_SETUP_OPTIONS			 \
	"  --pgctl       path to pg_ctl\n" \
	"  --pgdata      path to data director\n" \
	"  --pghost      PostgreSQL's hostname\n" \
	"  --pgport      PostgreSQL's port number\n" \
	"  --listen      PostgreSQL's listen_addresses\n" \
	"  --username    PostgreSQL's username\n" \
	"  --dbname      PostgreSQL's database name\n" \
	"  --nodename    pg_auto_failover node\n" \
	"  --formation   pg_auto_failover formation\n" \
	"  --group       pg_auto_failover group Id\n" \
	"  --monitor     pg_auto_failover Monitor Postgres URL\n" \

#define KEEPER_CLI_ALLOW_RM_PGDATA_OPTION \
	"  --allow-removing-pgdata   Allow pg_autoctl to remove the database directory\n"

#define KEEPER_CLI_PGDATA_OPTION \
	"  --pgdata      path to data directory\n" \


/* cli_do.c */
extern CommandLine do_commands;

/* cli_config.c */
extern CommandLine config_commands;

/* cli_create_drop_node.c */
extern CommandLine create_monitor_command;
extern CommandLine create_postgres_command;
extern CommandLine drop_node_command;

/* cli_enable_disable.c */
extern CommandLine enable_commands;
extern CommandLine disable_commands;

/* cli_formation.c */
extern CommandLine create_formation_command;
extern CommandLine drop_formation_command;

/* cli_service.c */
extern CommandLine service_run_command;
extern CommandLine service_stop_command;
extern CommandLine service_reload_command;

/* cli_show.c */
extern CommandLine show_uri_command;
extern CommandLine show_events_command;
extern CommandLine show_state_command;
extern CommandLine show_ipaddr_command;
extern CommandLine show_cidr_command;


int cli_create_node_getopts(int argc, char **argv,
							struct option *long_options,
							const char *optstring,
							KeeperConfig *options);
int keeper_cli_getopt_pgdata(int argc, char **argv);

void set_first_pgctl(PostgresSetup *pgSetup);
bool monitor_init_from_pgsetup(Monitor *monitor, PostgresSetup *pgSetup);

void exit_unless_role_is_keeper(KeeperConfig *kconfig);

/* cli_create_drop_node.c */
bool cli_create_config(Keeper *keeper, KeeperConfig *config);
void cli_create_pg(Keeper *keeper, KeeperConfig *config);



#endif  /* CLI_COMMON_H */
