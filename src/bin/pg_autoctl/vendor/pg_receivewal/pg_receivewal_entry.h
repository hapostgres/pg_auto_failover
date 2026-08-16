/*
 * src/bin/pg_autoctl/vendor/pg_receivewal/pg_receivewal_entry.h
 *   Entry point and WAL-segment-closed hook for this project's own
 *   in-process copy of pg_receivewal -- see pg_receivewal.c's own header
 *   comment for the full vendoring rationale and the two changes made to
 *   otherwise-unmodified upstream source.
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef PG_RECEIVEWAL_ENTRY_H
#define PG_RECEIVEWAL_ENTRY_H

#include "access/xlogdefs.h"

/*
 * Called from stop_streaming() (pg_receivewal.c) the moment a WAL segment
 * finishes (segment_finished, receivelog.h's own stream_stop_callback
 * comment) -- xlogpos is that segment's own end boundary, timeline the
 * timeline it was captured on. Runs synchronously, in the same forked
 * pg_receivewal process, on the streaming loop's own thread of control:
 * keep it fast and non-blocking, the same rule a real archive_command
 * script has to follow, for the same reason (it gates how quickly the
 * next segment gets to start).
 */
typedef void (*WalSegmentClosedHook) (XLogRecPtr xlogpos, uint32 timeline);

extern WalSegmentClosedHook pgaf_wal_segment_closed_hook;

/*
 * pg_receivewal_main is upstream's own main(), renamed and no longer the
 * process entry point -- called directly by service_archiver_pgreceivewal_
 * ctl.c's own forked child, argc/argv shaped exactly like the real
 * pg_receivewal binary's command line (this project builds that same argv
 * today; only the call site changed from execv() to a direct call).
 */
extern int pg_receivewal_main(int argc, char **argv);

#endif                          /* PG_RECEIVEWAL_ENTRY_H */
