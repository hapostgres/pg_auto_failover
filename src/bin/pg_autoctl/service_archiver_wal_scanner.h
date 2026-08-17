/*
 * src/bin/pg_autoctl/service_archiver_wal_scanner.h
 *   The periodic WAL-cache-directory scanner: the bounded correctness
 *   backstop for whatever service_archiver.c's WAL-notify socket alone
 *   might miss. See service_archiver_wal_scanner.c for the full design.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SERVICE_ARCHIVER_WAL_SCANNER_H
#define SERVICE_ARCHIVER_WAL_SCANNER_H

#include "keeper.h"

bool service_archiver_wal_scanner_start(void *context, pid_t *pid);

#endif                          /* SERVICE_ARCHIVER_WAL_SCANNER_H */
