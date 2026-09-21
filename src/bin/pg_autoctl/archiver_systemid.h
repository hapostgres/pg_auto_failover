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

/*
 * The primary's WAL segment size in bytes, as recorded by pg_receivewal in
 * <membershipDir>/archiver-walsegsize; 16MiB when absent or invalid.
 */
uint64_t archiver_walsegsize_read(const char *membershipDir);

/*
 * Absolute segment number (segno, as in XLByteToSeg) of a 24-hex WAL
 * segment file name, for a WAL segment size of segsize bytes.
 */
bool archiver_wal_name_is_hex24(const char *name);
uint64_t archiver_wal_name_segno(const char *walFileName, uint64_t segsize);

/*
 * The "WAL floor": segno of the oldest retained base backup's start
 * segment, persisted in <membershipDir>/archiver-wal-floor. Segments below
 * it are never needed and are neither kept nor re-reported.
 */
bool archiver_wal_floor_read(const char *membershipDir, uint64_t *segno);
bool archiver_wal_floor_write(const char *membershipDir, uint64_t segno);

#endif                          /* ARCHIVER_SYSTEMID_H */
