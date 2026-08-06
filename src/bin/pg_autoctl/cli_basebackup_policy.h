/*
 * src/bin/pg_autoctl/cli_basebackup_policy.h
 *   CLI for pgautofailover.basebackup_policy: create/show/set a named
 *   base-backup production/retention policy on the monitor.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef CLI_BASEBACKUP_POLICY_H
#define CLI_BASEBACKUP_POLICY_H

#include "commandline.h"

extern CommandLine create_basebackup_policy_command;
extern CommandLine show_basebackup_policy_command;
extern CommandLine set_basebackup_policy_command;

#endif /* CLI_BASEBACKUP_POLICY_H */
