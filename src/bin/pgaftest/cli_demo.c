/*
 * src/bin/pgaftest/cli_demo.c
 *   Demo application sub-command for pgaftest (moved from pg_autoctl do demo).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <stdio.h>
#include "commandline.h"
#include "cli_demo.h"
#include "log.h"

/*
 * The demo sub-commands forward to pg_autoctl's demoapp implementation.
 * For now we provide a stub that guides the user; the full implementation
 * is linked from the shared pg_autoctl sources.
 */

static void
cli_demo_run(int argc, char **argv)
{
	log_error("pgaftest demo run: not yet implemented in this build.");
	log_info("Use: docker compose exec <node> pg_autoctl do demo run ...");
}

static void
cli_demo_uri(int argc, char **argv)
{
	log_error("pgaftest demo uri: not yet implemented in this build.");
}

static void
cli_demo_ping(int argc, char **argv)
{
	log_error("pgaftest demo ping: not yet implemented in this build.");
}

static void
cli_demo_summary(int argc, char **argv)
{
	log_error("pgaftest demo summary: not yet implemented in this build.");
}

static CommandLine demo_run =
	make_command("run",     "Run the demo app",     "", "", NULL, cli_demo_run);
static CommandLine demo_uri =
	make_command("uri",     "Show app URI",         "", "", NULL, cli_demo_uri);
static CommandLine demo_ping =
	make_command("ping",    "Ping the app URI",     "", "", NULL, cli_demo_ping);
static CommandLine demo_summary =
	make_command("summary", "Show demo summary",    "", "", NULL, cli_demo_summary);

static CommandLine *demo_subcommands[] = {
	&demo_run,
	&demo_uri,
	&demo_ping,
	&demo_summary,
	NULL
};

CommandLine pgaftest_demo_command =
	make_command_set("demo",
	                 "Demo application for pg_auto_failover",
	                 "", "",
	                 NULL, demo_subcommands);
