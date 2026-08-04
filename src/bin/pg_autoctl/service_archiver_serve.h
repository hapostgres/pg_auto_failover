/*
 * src/bin/pg_autoctl/service_archiver_serve.h
 *   Archiving & Disaster Recovery: supervision of the pg_walsender child
 *   process an archiver runs to serve its captured WAL and base backups to
 *   downstream consumers (warm standbies, PITR nodes, `create postgres
 *   --from-archiver` rebuilds) -- the inbound counterpart to
 *   service_archiver.c's outbound pg_receivewal supervision. See
 *   ~/dev/temp/archiving-disaster-recovery.md and
 *   src/bin/pg_walsender/walsender.h for the protocol this serves.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SERVICE_ARCHIVER_SERVE_H
#define SERVICE_ARCHIVER_SERVE_H

#include "keeper.h"

void service_archiver_serve_set_port(int port);

bool service_archiver_serve_start_walsender(Keeper *keeper);
bool service_archiver_serve_stop_walsender(void);
bool service_archiver_serve_walsender_is_running(void);

bool service_archiver_serve_refresh_routes(Keeper *keeper);

bool service_archiver_serve_loop(Keeper *keeper);

#endif /* SERVICE_ARCHIVER_SERVE_H */
