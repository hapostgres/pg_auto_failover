/*
 * src/bin/pg_autoctl/service_archiver_run.h
 *   Archiving & Disaster Recovery: `pg_autoctl run` support for
 *   kind = archiver (milestone 3's own build-order line). Supervises the
 *   archiver's two halves -- WAL capture (service_archiver.c's
 *   service_archiver_loop, outbound pg_receivewal against the primary)
 *   and serving (service_archiver_serve.c's service_archiver_serve_loop,
 *   inbound pg_walsender) -- as two real supervisor.c Service[] entries
 *   under one supervised process tree, restart-on-crash, the same way
 *   start_keeper() already supervises postgres + node-active together for
 *   an ordinary node. Replaces needing two separately-managed processes
 *   (`create archiver --run` for capture, `archiver serve` for serving)
 *   with the one unified entry point operators already expect from
 *   `pg_autoctl run`.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SERVICE_ARCHIVER_RUN_H
#define SERVICE_ARCHIVER_RUN_H

#include "keeper.h"

bool start_archiver(Keeper *keeper);

#endif /* SERVICE_ARCHIVER_RUN_H */
