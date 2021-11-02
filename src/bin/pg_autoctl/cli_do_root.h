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

/* src/bin/pg_autoctl/cli_do_service.c */
extern CommandLine do_service_commands;
extern CommandLine do_service_postgres_ctl_commands;

/* src/bin/pg_autoctl/cli_do_show.c */
extern CommandLine do_show_commands;
extern CommandLine do_pgsetup_commands;
extern CommandLine do_service_postgres_ctl_commands;
extern CommandLine do_service_commands;

/* src/bin/pg_autoctl/cli_do_demo.c */
extern CommandLine do_demo_commands;

/* src/bin/pg_autoctl/cli_do_root.c */
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
extern CommandLine do_primary_identify_system;

extern CommandLine do_primary_;
extern CommandLine *do_primary[];

extern CommandLine do_standby_;
extern CommandLine *do_standby[];
extern CommandLine do_standby_init;
extern CommandLine do_standby_rewind;
extern CommandLine do_standby_promote;

extern CommandLine do_discover;

extern CommandLine do_tmux_commands;

/* src/bin/pg_autoctl/cli_do_azure.c */
extern CommandLine do_azure_ssh;

extern CommandLine do_commands;
extern CommandLine *do_subcommands[];

int keeper_cli_keeper_setup_getopts(int argc, char **argv);


/* src/bin/pg_autoctl/cli_do_misc.c */
void keeper_cli_create_replication_slot(int argc, char **argv);
void keeper_cli_drop_replication_slot(int argc, char **argv);
void keeper_cli_enable_synchronous_replication(int argc, char **argv);
void keeper_cli_disable_synchronous_replication(int argc, char **argv);

void keeper_cli_pgsetup_pg_ctl(int argc, char **argv);
void keeper_cli_pgsetup_discover(int argc, char **argv);
void keeper_cli_pgsetup_is_ready(int argc, char **argv);
void keeper_cli_pgsetup_wait_until_ready(int argc, char **argv);
void keeper_cli_pgsetup_startup_logs(int argc, char **argv);
void keeper_cli_pgsetup_tune(int argc, char **argv);

void keeper_cli_add_default_settings(int argc, char **argv);
void keeper_cli_create_monitor_user(int argc, char **argv);
void keeper_cli_create_replication_user(int argc, char **argv);
void keeper_cli_add_standby_to_hba(int argc, char **argv);
void keeper_cli_init_standby(int argc, char **argv);
void keeper_cli_rewind_old_primary(int argc, char **argv);
void keeper_cli_maybe_do_crash_recovery(int argc, char **argv);
void keeper_cli_promote_standby(int argc, char **argv);
void keeper_cli_receiwal(int argc, char **argv);
void keeper_cli_identify_system(int argc, char **argv);

/* src/bin/pg_autoctl/cli_do_tmux.c */
int cli_do_tmux_script_getopts(int argc, char **argv);
void cli_do_tmux_script(int argc, char **argv);
void cli_do_tmux_session(int argc, char **argv);
void cli_do_tmux_stop(int argc, char **argv);
void cli_do_tmux_clean(int argc, char **argv);
void cli_do_tmux_wait(int argc, char **argv);

/* src/bin/pg_autoctl/cli_do_tmux_compose.c */
void cli_do_tmux_compose_config(int argc, char **argv);
void cli_do_tmux_compose_script(int argc, char **argv);
void cli_do_tmux_compose_session(int argc, char **argv);

/* src/bin/pg_autoctl/cli_do_azure.c */
int cli_do_azure_getopts(int argc, char **argv);
void cli_do_azure_create_environment(int argc, char **argv);
void cli_do_azure_create_region(int argc, char **argv);
void cli_do_azure_drop_region(int argc, char **argv);
void cli_do_azure_deploy(int argc, char **argv);
void cli_do_azure_create_nodes(int argc, char **argv);
void cli_do_azure_ls(int argc, char **argv);
void cli_do_azure_show_ips(int argc, char **argv);
void cli_do_azure_ssh(int argc, char **argv);
void cli_do_azure_rsync(int argc, char **argv);
void cli_do_azure_show_state(int argc, char **argv);
void cli_do_azure_tmux_session(int argc, char **argv);
void cli_do_azure_tmux_kill(int argc, char **argv);

#endif  /* CLI_DO_ROOT_H */
