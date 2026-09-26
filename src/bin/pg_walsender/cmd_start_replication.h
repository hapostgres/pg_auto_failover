/*
 * src/bin/pg_walsender/cmd_start_replication.h
 *   START_REPLICATION [SLOT <name>] <startlsn> TIMELINE <tli>: streams WAL
 *   bytes straight out of the route's WAL cache directory, physical-only.
 *
 *   Deliberately does NOT vendor xlogreader.c for this: real walsender's
 *   own WalSndSegmentOpen (walsender.c) just computes a path from TLI+segno
 *   and opens it -- streaming raw bytes needs no WAL *record* decoding at
 *   all, only byte-range bookkeeping this file does directly. xlogreader.c
 *   would only earn its keep here for validating record boundaries, not
 *   required for a client (a real pg_receivewal) that already does its own
 *   validation on the bytes it receives.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_START_REPLICATION_H
#define WS_CMD_START_REPLICATION_H

#include "routes.h"

void cmd_start_replication(int sock, const WsRoute *route, const char *rawArgs);

#endif /* WS_CMD_START_REPLICATION_H */
