/*
 * src/bin/pg_walsender/defaults.h
 *   A handful of constants pg_walsender needs that would otherwise come
 *   from pg_autoctl/defaults.h -- duplicated rather than included, since
 *   pg_walsender is deliberately a standalone binary that does not link
 *   any of pg_autoctl's own sources (see
 *   ~/dev/temp/archiving-disaster-recovery.md). Keep
 *   PG_AUTOCTL_REPLICA_USERNAME in sync with pg_autoctl/defaults.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_DEFAULTS_H
#define WS_DEFAULTS_H

#include "postgres_fe.h"

#define PG_AUTOCTL_REPLICA_USERNAME "pgautofailover_replicator"

#define WS_DEFAULT_PORT 6543

/*
 * Reported as the "server_version" startup parameter so that real libpq
 * clients (pg_basebackup, pg_receivewal) compute a sane PQserverVersion().
 * PG_VERSION/PG_VERSION_NUM (from pg_config.h, pulled in via postgres_fe.h)
 * are this build's own real target version -- pg_walsender is built once
 * per PGVERSION, against that version's own server headers (Makefile.common's
 * pg_config --includedir-server), so this is already the archived group's
 * actual pg_version, not a stand-in for it. A previous fixed "16.4" value
 * here made every non-PG16 build report a version mismatch to real
 * pg_basebackup/pg_receivewal clients ("incompatible server version").
 */
#define WS_SERVER_VERSION PG_VERSION
#define WS_SERVER_VERSION_NUM PG_VERSION_NUM

#endif /* WS_DEFAULTS_H */
