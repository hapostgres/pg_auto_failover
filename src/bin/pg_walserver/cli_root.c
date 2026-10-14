/*
 * src/bin/pg_walserver/cli_root.c
 *   Top-level sub-command table for pg_walserver, built on this project's
 *   own command-line framework (src/bin/lib/subcommands.c/commandline.h),
 *   the same way pg_autoctl's own cli_root.c assembles its own tree: each
 *   command-area file (cli_serve.c, cli_cluster.c, cli_list.c, ...) owns
 *   its own getopt parser, its own CommandLine wiring, and its own run
 *   function together, and this file only pulls in each one's own header
 *   to assemble the whole tree below. See each cli_*.h's own header
 *   comment for what that sub-command actually does; README.md has the
 *   full design.
 *
 *   No sub-command is ever implicit: `pg_walserver` alone (or any
 *   unrecognized/missing sub-command) only prints usage and exits
 *   non-zero -- `pg_walserver serve ...` must always be spelled out, the
 *   same as any other sub-command-dispatching command in this project.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include "commandline.h"

#include "cli_archive.h"
#include "cli_archive_cleanup.h"
#include "cli_basebackup.h"
#include "cli_cluster.h"
#include "cli_create_cert.h"
#include "cli_fetch_systemid.h"
#include "cli_internal.h"
#include "cli_list.h"
#include "cli_ls.h"
#include "cli_ps.h"
#include "cli_reload.h"
#include "cli_restore_wal.h"
#include "cli_root.h"
#include "cli_scram_secret.h"
#include "cli_serve.h"
#include "cli_setup.h"
#include "cli_status.h"
#include "cli_stop.h"


/*
 * pg_walserver help
 */

/*
 * cli_help_run prints the whole sub-command tree at once, the exact same
 * "pg_autoctl help" facility (commandline_print_command_tree(),
 * src/bin/lib/subcommands.c/commandline.h) already provides.
 */
static void
cli_help_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	commandline_print_command_tree(&ws_root, stdout);
}


static CommandLine help_command =
	make_command("help", "Print this whole sub-command tree at once", "",
				 "", NULL, cli_help_run);


/*
 * Root command table
 */

static CommandLine *root_subcommands[] = {
	&serve_command,
	&scram_secret_command,
	&fetch_systemid_command,
	&basebackup_command,
	&setup_command,
	&cluster_commands,
	&create_cert_command,
	&archive_command,
	&restore_command,
	&archive_cleanup_command,
	&reload_command,
	&stop_command,
	&ps_command,
	&ls_command,
	&status_command,
	&list_commands,
	&help_command,
	&internal_commands,
	NULL
};

CommandLine ws_root =
	make_command_set("pg_walserver",
					 "The archiver's own replication-protocol server",
					 "serve ... | scram-secret ... | setup ... | "
					 "cluster ... | fetch-systemid ... | basebackup ... | "
					 "create-cert ... | archive-wal ... | restore-wal ... | "
					 "archive-cleanup ... | reload ... | stop ... | ps ... | "
					 "ls ... | status ... | list ... | help",
					 NULL, NULL, root_subcommands);
