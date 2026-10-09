/*
 * src/bin/pg_walserver/backup_bootstrap.h
 *   A one-shot, plain fork() (no execv()) that takes a single cluster's very
 *   first base backup in the background, reusing `pg_walserver basebackup`'s
 *   own logic (cli_basebackup.c) in-process -- see backup_bootstrap.c's own
 *   header comment for the full design, and accept_loop.c's own
 *   ws_bootstrap_missing_backups() for who calls this and when.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_BACKUP_BOOTSTRAP_H
#define WS_BACKUP_BOOTSTRAP_H

#include <stdbool.h>
#include <sys/types.h>

#include "postgres_fe.h"

#include "clusters.h"

bool ws_backup_bootstrap_start(const WsCluster *cluster, pid_t *pidOut);

#endif /* WS_BACKUP_BOOTSTRAP_H */
