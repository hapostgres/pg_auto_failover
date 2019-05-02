/*-------------------------------------------------------------------------
 *
 * src/monitor/conninfo.h
 *
 * Declarations for public functions and types related to reading
 * connection info from recovery.conf.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#pragma once

extern int ReadPrimaryHostAddress(char **primaryName, char **primaryPort);
