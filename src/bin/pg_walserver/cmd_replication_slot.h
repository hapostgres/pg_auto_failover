/*
 * src/bin/pg_walserver/cmd_replication_slot.h
 *   CREATE_REPLICATION_SLOT / READ_REPLICATION_SLOT / DROP_REPLICATION_SLOT,
 *   physical slots only (that is all that is supported). A slot here is a
 *   bookkeeping marker file under the route's WAL cache directory -- not a
 *   real Postgres slot on a live server (there's no live server). Its own
 *   "restart_lsn" is kept current the same way a real slot's is: cmd_
 *   start_replication.c advances it from the standby's own feedback
 *   (StandbyStatusUpdate's "flush" field) while streaming against it, and
 *   cli_archive_cleanup.c (ws_replication_slot_oldest_restart_lsn(), below)
 *   refuses to remove any WAL a still-existing slot's own restart_lsn
 *   still needs -- unconditionally, the same as a real Postgres slot,
 *   with no flag to opt out short of dropping the slot itself.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CMD_REPLICATION_SLOT_H
#define WS_CMD_REPLICATION_SLOT_H

#include <stdint.h>

#include "routes.h"

/*
 * slotName/temporary/isLogical/wait are already parsed out by repl_gram.y's
 * grammar (see repl_command.h) -- this file no longer tokenizes the raw
 * command text itself, see its own removed parse_slot_name().
 */
void cmd_create_replication_slot(int sock, const WsRoute *route,
								 const char *slotName, bool temporary,
								 bool isLogical);
void cmd_read_replication_slot(int sock, const WsRoute *route, const char *slotName);
void cmd_drop_replication_slot(int sock, const WsRoute *route,
							   const char *slotName, bool wait);

/*
 * Slot names are [a-z0-9_]{1,63}, as in PostgreSQL; a route holds at most 64
 * slots (a clear error beyond); a slot file is written atomically (temp +
 * rename). CREATE of an existing slot is an error (42710) and never resets
 * it.
 */

/*
 * ws_replication_slot_exists is true when slotName's own marker file is
 * present under route -- cmd_start_replication.c's own check that a named
 * SLOT clause refers to a real slot, the same requirement a real
 * PostgreSQL walsender enforces (START_REPLICATION SLOT of a slot that
 * does not exist is refused, never silently ignored).
 */
bool ws_replication_slot_exists(const WsRoute *route, const char *slotName);

/*
 * ws_replication_slot_read_restart_lsn reads slotName's own current
 * "restart_lsn" into lsnOut ("%X/%08X" shape). Returns false (lsnOut
 * untouched) when the slot does not exist or its marker file could not be
 * read.
 */
bool ws_replication_slot_read_restart_lsn(const WsRoute *route,
										  const char *slotName,
										  char *lsnOut, size_t lsnOutSize);

/*
 * ws_replication_slot_update_restart_lsn overwrites slotName's own
 * "restart_lsn" with lsn -- cmd_start_replication.c's own use, advancing
 * an existing slot as the standby's own feedback reports further
 * progress (see cmd_start_replication.c's own header comment for the full
 * mechanism: which field is trusted, how often this is actually called).
 * Never creates a slot: returns false (nothing written) if slotName does
 * not already exist -- streaming against a nonexistent slot is refused
 * before this could ever be reached anyway (see ws_replication_slot_
 * exists() above), so this is a defensive, not a load-bearing, check.
 * The caller alone decides whether lsn is actually an advance -- this
 * function itself does not compare against what's already on disk.
 */
bool ws_replication_slot_update_restart_lsn(const WsRoute *route,
											const char *slotName,
											const char *lsn);

/*
 * ws_replication_slot_oldest_restart_lsn scans every slot under route and
 * reports the one whose own "restart_lsn" is oldest (the smallest WAL
 * segment number at segSize) -- cli_archive_cleanup.c's own use: the
 * floor beyond which retention must never remove WAL a still-existing
 * slot needs. Returns false (both Out parameters untouched) when route
 * has no slots at all, the ordinary case today. slotNameOut/lsnOut, when
 * true is returned, are only ever used for logging which slot is
 * responsible -- ties (more than one slot at the same oldest position)
 * report whichever is found first, an arbitrary but harmless choice.
 */
bool ws_replication_slot_oldest_restart_lsn(const WsRoute *route,
											uint64_t segSize,
											char *slotNameOut,
											size_t slotNameOutSize,
											char *lsnOut, size_t lsnOutSize);

#endif /* WS_CMD_REPLICATION_SLOT_H */
