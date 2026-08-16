/*
 * src/bin/pg_autoctl/service_archiver_pgreceivewal_ctl.h
 *   pg_receivewal's own dedicated, supervised controller process -- see
 *   service_archiver_pgreceivewal_ctl.c for the full design, and service_
 *   archiver_pgreceivewal_state.h for the thin control-plane API most
 *   callers actually want instead of this file (this one pulls in the
 *   vendored pg_receivewal binary and its libraries; that one doesn't).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SERVICE_ARCHIVER_PGRECEIVEWAL_CTL_H
#define SERVICE_ARCHIVER_PGRECEIVEWAL_CTL_H

#include "keeper.h"

bool service_archiver_pgreceivewal_ctl_start(void *context, pid_t *pid);
void service_archiver_pgreceivewal_ctl_loop(KeeperConfig *config);

#endif                          /* SERVICE_ARCHIVER_PGRECEIVEWAL_CTL_H */
