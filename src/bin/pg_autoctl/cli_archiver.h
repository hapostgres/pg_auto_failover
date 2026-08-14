/*
 * src/bin/pg_autoctl/cli_archiver.h
 *   pg_autoctl archiver -- the archiver's own command group. Only `serve`
 *   is implemented this milestone; the other CLI-reference subverbs
 *   (add-storage, remove-storage, backup, prefetch, ...) belong to later
 *   milestones and stay unregistered until then, per
 *   ~/dev/temp/archiving-disaster-recovery.md's Build order.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef CLI_ARCHIVER_H
#define CLI_ARCHIVER_H

#include "commandline.h"
#include "keeper_config.h"
#include "monitor.h"

extern CommandLine archiver_serve_command;
extern CommandLine *archiver_subcommands[];
extern CommandLine archiver_commands;

/*
 * Shared by `pg_autoctl archiver show state` and `pg_autoctl show state`
 * (cli_show.c), which delegates to this when the local configuration
 * file's pg_autoctl.nodekind is "archiver" rather than an ordinary node.
 */
void cli_print_archiver_state(Monitor *monitor, KeeperConfig *config);

#endif /* CLI_ARCHIVER_H */
