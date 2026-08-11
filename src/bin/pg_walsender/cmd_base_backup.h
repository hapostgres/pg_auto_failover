/*
 * src/bin/pg_walsender/cmd_base_backup.h
 *   BASE_BACKUP: streams the backup directory named by route->path's own
 *   basebackups/.latest pointer as a ustar archive over the real
 *   multiplexed-COPY-stream wire format modern (>= 15) pg_basebackup
 *   clients expect (traced from
 *   /Users/dim/dev/PostgreSQL/postgresql's src/backend/backup/
 *   basebackup_copy.c and src/bin/pg_basebackup/pg_basebackup.c -- see
 *   this file's own .c for the exact message sequence, with citations).
 *
 *   MVP scope: a single archive (the base directory itself, no separate
 *   tablespaces), no server-side compression, no backup manifest, no
 *   WAL-inclusive backup (`-X none` on the client side) -- each rejected
 *   up front with a clean ErrorResponse rather than silently ignored.
 *   do_pg_backup_start()/do_pg_backup_stop() (live-instance, backend-only)
 *   are never called: the resolved backup directory is already a
 *   complete, at-rest backup (produced by a real pg_basebackup run
 *   against a live server, pg_autoctl's own service_archiver_basebackup.c),
 *   so the start/end LSN this command reports comes from that backup's
 *   own backup_label file, not a live checkpoint.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_BASE_BACKUP_H
#define WS_CMD_BASE_BACKUP_H

#include "routes.h"

void cmd_base_backup(int sock, const WsRoute *route, const char *rawOptions);

#endif /* WS_CMD_BASE_BACKUP_H */
