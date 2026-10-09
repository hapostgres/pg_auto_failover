/*
 * src/bin/pg_walserver/cmd_replication_slot.h
 *   CREATE_REPLICATION_SLOT / READ_REPLICATION_SLOT / DROP_REPLICATION_SLOT,
 *   physical slots only (that is all that is supported). A slot here is a
 *   bookkeeping marker file under the cluster's WAL cache directory -- not a
 *   real Postgres slot on a live server (there's no live server). Its own
 *   "restart_lsn" is kept current the same way a real slot's is: cmd_
 *   start_replication.c advances it from the standby's own feedback
 *   (StandbyStatusUpdate's "flush" field) while streaming against it, and
 *   cli_archive_cleanup.c (ws_replication_slot_oldest_restart_lsn(), below)
 *   refuses to remove any WAL a still-existing slot's own restart_lsn
 *   still needs -- unconditionally, the same as a real Postgres slot,
 *   with no flag to opt out short of dropping the slot itself.
 *
 *   At most one START_REPLICATION session may stream against a given
 *   slot at a time (ws_replication_slot_try_lock()/_unlock(), below),
 *   the same "one active connection per slot" rule a real PostgreSQL
 *   walsender enforces -- without it, two concurrent sessions could each
 *   persist their own "restart_lsn" independently, and whichever wrote
 *   last would win regardless of which was actually further ahead.
 *
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_REPLICATION_SLOT_H
#define WS_CMD_REPLICATION_SLOT_H

#include <stdint.h>

#include "clusters.h"

/*
 * slotName/temporary/isLogical/wait are already parsed out by repl_gram.y's
 * grammar (see repl_command.h) -- this file no longer tokenizes the raw
 * command text itself, see its own removed parse_slot_name().
 */
void cmd_create_replication_slot(int sock, const WsCluster *cluster,
								 const char *slotName, bool temporary,
								 bool isLogical);
void cmd_read_replication_slot(int sock, const WsCluster *cluster, const char *slotName);
void cmd_drop_replication_slot(int sock, const WsCluster *cluster,
							   const char *slotName, bool wait);

/*
 * Slot names are [a-z0-9_]{1,63}, as in PostgreSQL; a cluster holds at most 64
 * slots (a clear error beyond); a slot file is written atomically (temp +
 * rename). CREATE of an existing slot is an error (42710) and never resets
 * it.
 */

bool ws_replication_slot_exists(const WsCluster *cluster, const char *slotName);

bool ws_replication_slot_read_restart_lsn(const WsCluster *cluster,
										  const char *slotName,
										  char *lsnOut, size_t lsnOutSize);

bool ws_replication_slot_update_restart_lsn(const WsCluster *cluster,
											const char *slotName,
											const char *lsn);

bool ws_replication_slot_oldest_restart_lsn(const WsCluster *cluster,
											uint64_t segSize,
											char *slotNameOut,
											size_t slotNameOutSize,
											char *lsnOut, size_t lsnOutSize);

/*
 * ws_replication_slot_try_lock serializes concurrent START_REPLICATION
 * sessions against the same slot, the same "one active connection per
 * slot" rule a real PostgreSQL walsender enforces -- see cmd_
 * replication_slot.c's own comment for the full mechanism (a dedicated,
 * never-renamed ".lock" file, flock()'d for the whole session, released
 * automatically on close() including an unclean process exit). Returns
 * an open fd to hold for the session's own duration (release with ws_
 * replication_slot_unlock()), or -1 -- already active elsewhere, or some
 * other error, either way already logged -- when the lock could not be
 * acquired.
 */
int ws_replication_slot_try_lock(const WsCluster *cluster, const char *slotName);

/*
 * ws_replication_slot_unlock releases a lock ws_replication_slot_try_
 * lock() returned. Safe to call with fd < 0 (nothing was ever locked).
 */
void ws_replication_slot_unlock(int fd);

#endif /* WS_CMD_REPLICATION_SLOT_H */
