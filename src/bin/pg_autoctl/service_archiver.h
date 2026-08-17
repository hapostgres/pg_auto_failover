/*
 * src/bin/pg_autoctl/service_archiver.h
 *   Archiving & Disaster Recovery: supervision of the pg_receivewal child
 *   process an ARCHIVING node keeps running against its group's current
 *   primary.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SERVICE_ARCHIVER_H
#define SERVICE_ARCHIVER_H

#include "keeper.h"
#include "pgsql.h"

bool service_archiver_start_pgreceivewal(Keeper *keeper, NodeAddress *primaryNode);
bool service_archiver_stop_pgreceivewal(Keeper *keeper);
bool service_archiver_pgreceivewal_is_running(Keeper *keeper);

bool service_archiver_report_captured_wal(Keeper *keeper);

bool service_archiver_loop(Keeper *keeper);

#endif /* SERVICE_ARCHIVER_H */
