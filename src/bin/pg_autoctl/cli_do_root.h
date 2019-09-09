/*
 * src/bin/pg_autoctl/cli_do_root.h
 *     Implementation of a CLI which lets you run individual keeper routines
 *     directly
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef CLI_DO_ROOT_H
#define CLI_DO_ROOT_H

#include "commandline.h"

/* src/bin/pg_autoctl/cli_do_fsm.c */
extern CommandLine do_fsm_commands;

/* src/bin/pg_autoctl/cli_do_monitor.c */
extern CommandLine do_monitor_commands;

/* src/bin/pg_autoctl/cli_do_show.c */
extern CommandLine do_show_commands;

/* src/bin/pg_autoctl/cli_do_root.c */
extern CommandLine do_destroy;
extern CommandLine do_httpd;

extern CommandLine do_primary_adduser;
extern CommandLine *do_primary_adduser_subcommands[];
extern CommandLine do_primary_adduser_monitor;
extern CommandLine do_primary_adduser_replica;

extern CommandLine do_primary_syncrep_;
extern CommandLine *do_primary_syncrep[];
extern CommandLine do_primary_syncrep_enable;
extern CommandLine do_primary_syncrep_disable;

extern CommandLine do_primary_slot_;
extern CommandLine *do_primary_slot[];
extern CommandLine do_primary_slot_create;
extern CommandLine do_primary_slot_drop;

extern CommandLine do_primary_hba;
extern CommandLine *do_primary_hba_commands[];
extern CommandLine do_primary_hba_setup;

extern CommandLine do_primary_defaults;

extern CommandLine do_primary_;
extern CommandLine *do_primary[];

extern CommandLine do_standby_;
extern CommandLine *do_standby[];
extern CommandLine do_standby_init;
extern CommandLine do_standby_rewind;
extern CommandLine do_standby_promote;

extern CommandLine do_discover;

extern CommandLine do_commands;
extern CommandLine *do_subcommands[];

int keeper_cli_keeper_setup_getopts(int argc, char **argv);
void stop_postgres_and_remove_pgdata_and_config(ConfigFilePaths *pathnames,
												PostgresSetup *pgSetup);

/* src/bin/pg_autoctl/cli_do_misc.c */
void keeper_cli_destroy_node(int argc, char **argv);
void keeper_cli_httpd_start(int argc, char **argv);
void keeper_cli_create_replication_slot(int argc, char **argv);
void keeper_cli_drop_replication_slot(int argc, char **argv);
void keeper_cli_enable_synchronous_replication(int argc, char **argv);
void keeper_cli_disable_synchronous_replication(int argc, char **argv);
void keeper_cli_discover_pg_setup(int argc, char **argv);
void keeper_cli_add_default_settings(int argc, char **argv);
void keeper_cli_create_monitor_user(int argc, char **argv);
void keeper_cli_create_replication_user(int argc, char **argv);
void keeper_cli_add_standby_to_hba(int argc, char **argv);
void keeper_cli_init_standby(int argc, char **argv);
void keeper_cli_rewind_old_primary(int argc, char **argv);
void keeper_cli_promote_standby(int argc, char **argv);

void keeper_cli_destroy_keeper_node(Keeper *keeper,
									KeeperConfig *config);


#endif  /* CLI_DO_ROOT_H */
