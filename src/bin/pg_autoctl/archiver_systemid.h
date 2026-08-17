/*
 * src/bin/pg_autoctl/archiver_systemid.h
 *   The small "which Postgres cluster incarnation is this membership's WAL
 *   from" file every archiver membership persists once it learns its own
 *   group's system identifier -- see archiver_systemid.c for the full
 *   design. Split out of service_archiver.c (the writer) so the readers
 *   (service_archiver_pgreceivewal_ctl.c's forked pg_receivewal child,
 *   service_archiver_wal_scanner.c) don't need to link anything beyond
 *   this thin file.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef ARCHIVER_SYSTEMID_H
#define ARCHIVER_SYSTEMID_H

#include <stdbool.h>
#include <stdint.h>

#include "keeper_config.h"

void archiver_systemid_path(KeeperConfig *config, char *dest, size_t destSize);
bool archiver_systemid_read(KeeperConfig *config, uint64_t *systemIdentifier);
bool archiver_systemid_read_from_path(const char *path,
									  uint64_t *systemIdentifier);

#endif                          /* ARCHIVER_SYSTEMID_H */
