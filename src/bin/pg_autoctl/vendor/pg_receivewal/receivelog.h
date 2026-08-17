/*-------------------------------------------------------------------------
 *
 * receivelog.h
 *
 * Portions Copyright (c) 1996-2021, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		  src/bin/pg_basebackup/receivelog.h
 *-------------------------------------------------------------------------
 */

#ifndef RECEIVELOG_H
#define RECEIVELOG_H

#include "access/xlogdefs.h"
#include "libpq-fe.h"
#include "walmethods.h"

/*
 * PGAF: this file is vendored from REL_14_STABLE (see pg_receivewal.c's
 * own header comment for why) -- common/logging.h renamed pg_log_fatal()
 * to pg_fatal() in a later release (a pure rename upstream, no semantic
 * change), so PG15+'s own common/logging.h no longer declares the name
 * this vendored source still calls. Included here, not in each .c file
 * individually, since receivelog.c/walmethods.c/pg_receivewal.c all
 * already include this header.
 */
#include "common/logging.h"
#if PG_VERSION_NUM >= 150000
#define pg_log_fatal pg_fatal
#endif

/*
 * Called before trying to read more data or when a segment is
 * finished. Return true to stop streaming.
 */
typedef bool (*stream_stop_callback) (XLogRecPtr segendpos, uint32 timeline, bool segment_finished);

/*
 * Global parameters when receiving xlog stream. For details about the individual fields,
 * see the function comment for ReceiveXlogStream().
 */
typedef struct StreamCtl
{
	XLogRecPtr	startpos;		/* Start position for streaming */
	TimeLineID	timeline;		/* Timeline to stream data from */
	char	   *sysidentifier;	/* Validate this system identifier and
								 * timeline */
	int			standby_message_timeout;	/* Send status messages this often */
	bool		synchronous;	/* Flush immediately WAL data on write */
	bool		mark_done;		/* Mark segment as done in generated archive */
	bool		do_sync;		/* Flush to disk to ensure consistent state of
								 * data */

	stream_stop_callback stream_stop;	/* Stop streaming when returns true */

	pgsocket	stop_socket;	/* if valid, watch for input on this socket
								 * and check stream_stop() when there is any */

	WalWriteMethod *walmethod;	/* How to write the WAL */
	char	   *partial_suffix; /* Suffix appended to partially received files */
	char	   *replication_slot;	/* Replication slot to use, or NULL */
} StreamCtl;



extern bool CheckServerVersionForStreaming(PGconn *conn);
extern bool ReceiveXlogStream(PGconn *conn,
							  StreamCtl *stream);

#endif							/* RECEIVELOG_H */
