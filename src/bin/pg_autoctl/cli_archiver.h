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

extern CommandLine archiver_serve_command;
extern CommandLine *archiver_subcommands[];
extern CommandLine archiver_commands;

#endif /* CLI_ARCHIVER_H */
