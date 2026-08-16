/*
 * src/bin/pg_autoctl/service_archiver_pgreceivewal_state.h
 *   The thin, control-plane-only half of pg_receivewal's own supervision:
 *   the desired-state file format and read/write/liveness-check API, with
 *   no dependency on the vendored pg_receivewal binary itself (vendor/
 *   pg_receivewal/) or its libraries.
 *
 *   Deliberately split out of service_archiver_pgreceivewal_ctl.h/.c
 *   (which still owns the actual controller process -- forking, running
 *   pg_receivewal_main(), reconciling): every caller that only needs to
 *   tell the controller what to do (service_archiver.c's FSM tick,
 *   fsm_transition.c) or ask what it's currently doing links this file
 *   alone, not the controller implementation or its vendored dependency
 *   chain -- pgaftest is exactly this kind of caller (it shares service_
 *   archiver.c/fsm_transition.c's real logic for tests, but never
 *   actually runs an archiver's own reconciler or controller process),
 *   and used to pull in vendor/pg_receivewal's ~3300 lines plus three
 *   extra static libraries (-lpgfeutils -lpgcommon -lpgport) purely to
 *   satisfy the linker before this split, never touching any of it at
 *   runtime.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SERVICE_ARCHIVER_PGRECEIVEWAL_STATE_H
#define SERVICE_ARCHIVER_PGRECEIVEWAL_STATE_H

#include "keeper.h"
#include "pgsql.h"

/*
 * The desired-state file's own values -- shared with service_archiver_
 * pgreceivewal_ctl.c, which is the only other reader (it polls this
 * struct's own on-disk form to reconcile the real pg_receivewal child
 * against it).
 */
typedef struct ArchiverPgReceivewalDesiredState
{
	bool running;
	char host[_POSIX_HOST_NAME_MAX];
	int port;
	char slot[MAXCONNINFO];
} ArchiverPgReceivewalDesiredState;

void archiver_pgreceivewal_state_path(KeeperConfig *config, char *dest);
void archiver_pgreceivewal_pidfile_path(KeeperConfig *config, char *dest);

bool service_archiver_pgreceivewal_set_desired_state(Keeper *keeper,
													 bool running,
													 NodeAddress *primaryNode);
bool archiver_pgreceivewal_read_desired_state(KeeperConfig *config,
											  ArchiverPgReceivewalDesiredState *desired);

bool service_archiver_pgreceivewal_ctl_is_running(KeeperConfig *config,
												  bool *isRunning);

#endif                          /* SERVICE_ARCHIVER_PGRECEIVEWAL_STATE_H */
