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

#define PG_AUTOCTL_REPLICA_USERNAME "pgautofailover_replicator"

#define WS_DEFAULT_PORT 6543

/*
 * Reported as the "server_version" startup parameter so that real libpq
 * clients (pg_basebackup, pg_receivewal) compute a sane PQserverVersion().
 * MVP: a fixed, reasonably-current value; wiring this to the archived
 * group's actual tracked pg_version (see the Postgres/Citus version
 * tracking prerequisite, milestone 0) is a follow-up, not required for the
 * protocol to function.
 */
#define WS_SERVER_VERSION "16.4"
#define WS_SERVER_VERSION_NUM 160004

#endif /* WS_DEFAULTS_H */
