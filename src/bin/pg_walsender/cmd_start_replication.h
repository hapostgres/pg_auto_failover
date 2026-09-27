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
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_START_REPLICATION_H
#define WS_CMD_START_REPLICATION_H

#include <stdint.h>

#include "routes.h"

/*
 * slotName/startLsn/haveTimeline/timeline are already parsed out by
 * repl_gram.y's grammar (see repl_command.h) -- this file no longer
 * tokenizes the raw command text itself. slotName is "" when the client
 * didn't send a SLOT clause (this file doesn't act on it either way, see
 * this header's own comment on retention). haveTimeline is false when the
 * client didn't send a TIMELINE clause, in which case the current
 * timeline is looked up from the WAL cache, same as before.
 */
void cmd_start_replication(int sock, const WsRoute *route,
						   const char *slotName, uint64_t startLsn,
						   bool haveTimeline, uint32_t timeline);

#endif /* WS_CMD_START_REPLICATION_H */
