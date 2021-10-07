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
extern bool createAndRun;
extern bool outputJSON;
extern bool openAppHBAonLAN;
extern bool dropAndDestroy;

#define SSL_CA_FILE_FLAG 1      /* root public certificate */
#define SSL_CRL_FILE_FLAG 2     /* certificates revocation list */
#define SSL_SERVER_CRT_FLAG 3   /* server.key (private key) */
#define SSL_SERVER_KEY_FLAG 4   /* server.crt (public certificate) */
#define SSL_MODE_FLAG 5         /* client side sslmode for connection strings */

extern int ssl_flag;

extern int monitorDisabledNodeId;

#define KEEPER_CLI_SSL_OPTIONS \
	"  --ssl-self-signed setup network encryption using self signed certificates (does NOT protect against MITM)\n" \
	"  --ssl-mode        use that sslmode in connection strings\n" \
	"  --ssl-ca-file     set the Postgres ssl_ca_file to that file path\n" \
	"  --ssl-crl-file    set the Postgres ssl_crl_file to that file path\n" \
	"  --no-ssl          don't enable network encryption (NOT recommended, prefer --ssl-self-signed)\n" \
	"  --server-key      set the Postgres ssl_key_file to that file path\n" \
	"  --server-cert     set the Postgres ssl_cert_file to that file path\n"

#define KEEPER_CLI_WORKER_SETUP_OPTIONS \
	"  --pgctl           path to pg_ctl\n" \
	"  --pgdata          path to data directory\n" \
	"  --pghost          PostgreSQL's hostname\n" \
	"  --pgport          PostgreSQL's port number\n" \
	"  --listen          PostgreSQL's listen_addresses\n" \
	"  --username        PostgreSQL's username\n" \
	"  --dbname          PostgreSQL's database name\n" \
	"  --proxyport       Proxy's port number\n" \
	"  --name            pg_auto_failover node name\n" \
	"  --hostname        hostname used to connect from the other nodes\n" \
	"  --formation       pg_auto_failover formation\n" \
	"  --group           pg_auto_failover group Id\n" \
	"  --monitor         pg_auto_failover Monitor Postgres URL\n" \
	KEEPER_CLI_SSL_OPTIONS

#define KEEPER_CLI_NON_WORKER_SETUP_OPTIONS \
	"  --pgctl           path to pg_ctl\n" \
	"  --pgdata          path to data directory\n" \
	"  --pghost          PostgreSQL's hostname\n" \
	"  --pgport          PostgreSQL's port number\n" \
	"  --listen          PostgreSQL's listen_addresses\n" \
	"  --username        PostgreSQL's username\n" \
	"  --dbname          PostgreSQL's database name\n" \
	"  --name            pg_auto_failover node name\n" \
	"  --hostname        hostname used to connect from the other nodes\n" \
	"  --formation       pg_auto_failover formation\n" \
	"  --group           pg_auto_failover group Id\n" \
	"  --monitor         pg_auto_failover Monitor Postgres URL\n" \
	KEEPER_CLI_SSL_OPTIONS

#define CLI_PGDATA_OPTION \
	"  --pgdata      path to data directory\n" \

#define CLI_PGDATA_USAGE " [ --pgdata ] [ --json ] "


/* cli_do.c */
extern CommandLine do_commands;

/* cli_config.c */
extern CommandLine config_commands;

/* cli_create_drop_node.c */
extern CommandLine create_monitor_command;
extern CommandLine create_postgres_command;
extern CommandLine drop_node_command;
extern CommandLine drop_monitor_command;
extern CommandLine destroy_command;

/* cli_get_set_properties.c */
extern CommandLine get_commands;
extern CommandLine set_commands;

/* cli_enable_disable.c */
extern CommandLine enable_commands;
extern CommandLine disable_commands;

/* cli_formation.c */
extern CommandLine create_formation_command;
extern CommandLine drop_formation_command;

/* cli_perform.c */
extern CommandLine perform_failover_command;
extern CommandLine perform_switchover_command;

extern CommandLine *perform_subcommands[];
extern CommandLine perform_commands;

/* cli_service.c */
extern CommandLine service_run_command;
extern CommandLine service_stop_command;
extern CommandLine service_reload_command;
extern CommandLine service_status_command;

/* cli_show.c */
extern CommandLine show_uri_command;
extern CommandLine show_events_command;
extern CommandLine show_state_command;
extern CommandLine show_settings_command;
extern CommandLine show_file_command;
extern CommandLine show_standby_names_command;

/* cli_watch.c */
extern CommandLine watch_command;

/* cli_systemd.c */
extern CommandLine systemd_cat_service_file_command;

/*
 * Handling SSL options on the command line and their inter-compatibility is a
 * little complex.
 */
typedef enum
{
	SSL_CLI_UNKNOWN = 0,
	SSL_CLI_NO_SSL,
	SSL_CLI_SELF_SIGNED,
	SSL_CLI_USER_PROVIDED
} SSLCommandLineOptions;

void keeper_cli_help(int argc, char **argv);
int cli_print_version_getopts(int argc, char **argv);
void keeper_cli_print_version(int argc, char **argv);
void cli_pprint_json(JSON_Value *js);

void cli_common_get_set_pgdata_or_exit(PostgresSetup *pgSetup);

int cli_common_keeper_getopts(int argc, char **argv,
							  struct option *long_options,
							  const char *optstring,
							  KeeperConfig *options,
							  SSLCommandLineOptions *sslCommandLineOptions);

int cli_create_node_getopts(int argc, char **argv,
							struct option *long_options,
							const char *optstring,
							KeeperConfig *options);

int cli_getopt_pgdata(int argc, char **argv);
void prepare_keeper_options(KeeperConfig *options);

void set_first_pgctl(PostgresSetup *pgSetup);
bool monitor_init_from_pgsetup(Monitor *monitor, PostgresSetup *pgSetup);

void exit_unless_role_is_keeper(KeeperConfig *kconfig);
void cli_set_groupId(Monitor *monitor, KeeperConfig *kconfig);

/* cli_create_drop_node.c */
bool cli_create_config(Keeper *keeper);
void cli_create_pg(Keeper *keeper);
bool check_or_discover_hostname(KeeperConfig *config);

int cli_drop_node_getopts(int argc, char **argv);
void cli_drop_node(int argc, char **argv);
void keeper_cli_destroy_node(int argc, char **argv);

void cli_drop_node_from_monitor(KeeperConfig *config,
								int64_t *nodeId, int *groupId);
void cli_drop_local_node(KeeperConfig *config, bool dropAndDestroy);

bool cli_getopt_ssl_flags(int ssl_flag, char *optarg, PostgresSetup *pgSetup);
bool cli_getopt_accept_ssl_options(SSLCommandLineOptions newSSLOption,
								   SSLCommandLineOptions currentSSLOptions);

char * logLevelToString(int logLevel);

bool cli_common_pgsetup_init(ConfigFilePaths *pathnames, PostgresSetup *pgSetup);
bool cli_common_ensure_formation(KeeperConfig *options);

bool cli_pg_autoctl_reload(const char *pidfile);

int cli_node_metadata_getopts(int argc, char **argv);
int cli_get_name_getopts(int argc, char **argv);
bool cli_use_monitor_option(KeeperConfig *options);
void cli_monitor_init_from_option_or_config(Monitor *monitor,
											KeeperConfig *kconfig);
void cli_ensure_node_name(Keeper *keeper);

bool discover_hostname(char *hostname, int size,
					   const char *monitorHostname, int monitorPort);

/* cli_get_set_properties.c */
void cli_get_formation_settings(int argc, char **argv);

#endif  /* CLI_COMMON_H */
