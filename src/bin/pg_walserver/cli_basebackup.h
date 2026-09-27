/*
 * src/bin/pg_walserver/cli_basebackup.h
 *   `pg_walserver basebackup`: a one-shot client that takes a real
 *   pg_basebackup of a route's upstream straight into
 *   "<path>/basebackups/<label>/", then atomically swaps
 *   "<path>/basebackups/.latest" to point at it -- the file
 *   cmd_base_backup.c's server side reads on every BASE_BACKUP request.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_BASEBACKUP_H
#define WS_CLI_BASEBACKUP_H

#include <stdbool.h>

#include "cli_upstream.h"

/*
 * cli_basebackup_run takes a real pg_basebackup of target's upstream into a
 * freshly created "<target->path>/basebackups/<label>" directory (label:
 * a UTC timestamp, matching service_archiver_basebackup.c's own scheme so
 * both the standalone and pgaf-integrated backups sit side by side without
 * a naming collision), validates the result (backup_label and PG_VERSION
 * both present -- pg_basebackup itself already guarantees a well-formed
 * backup_label on a zero exit, this is a defense against a partial result
 * rather than a re-parse of it), and only then atomically swaps
 * "<target->path>/basebackups/.latest" to the new label. Never touches
 * .latest on failure: a route always keeps serving its previous,
 * known-good backup until a new one actually completes.
 *
 * Returns true on success (labelOut, when not NULL, receives the new
 * backup's own label), false with an error already logged otherwise.
 */
bool cli_basebackup_run(const WsUpstreamTarget *target,
						char *labelOut, size_t labelOutSize);

#endif /* WS_CLI_BASEBACKUP_H */
