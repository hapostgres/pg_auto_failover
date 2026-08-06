/*
 * src/bin/pg_walsender/startup.h
 *   Startup-packet negotiation: SSL/GSS decline, protocol version check,
 *   and StartupMessage key/value parsing. Structurally mirrors real
 *   Postgres's ProcessStartupPacket() (backend_startup.c), reimplemented
 *   frontend-only -- that function is backend-locked (palloc/List/ereport,
 *   see the design research in ~/dev/temp/archiving-disaster-recovery.md's
 *   companion investigation), not something we can call into directly.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_STARTUP_H
#define WS_STARTUP_H

#include <stdbool.h>

#include "walsender.h"

/*
 * ws_startup_negotiate reads (and answers) SSLRequest/GSSENCRequest
 * probes until the client sends a real StartupMessage, then parses it into
 * *params. Returns false on any protocol error or if the client gives up
 * (socket already unusable at that point; caller should just close it).
 */
bool ws_startup_negotiate(int sock, WsStartupParams *params);

#endif /* WS_STARTUP_H */
