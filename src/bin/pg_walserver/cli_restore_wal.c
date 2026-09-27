/*
 * src/bin/pg_walserver/cli_restore_wal.c
 *   See cli_restore_wal.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>

#include "postgres_fe.h"

#include "cli_restore_wal.h"
#include "fetch_client.h"
#include "log.h"
#include "string_utils.h"

/*
 * ws_restore_run connects to target (pg_walserver itself, never a Postgres
 * primary -- see this file's own header comment for why it doesn't reuse
 * cli_upstream.c) and fetches filename into outputPath via
 * src/bin/common/fetch_client.c's own ws_fetch_file_client(), which does
 * the actual FETCH_FILE round trip and the same-directory-temp-file-plus-
 * rename dance that keeps a killed/interrupted restore from leaving a
 * partial file at outputPath. Returns true on success, false with an
 * error already logged (by ws_fetch_file_client() itself) on any failure,
 * including the ordinary "not found" case restore_command hits at the end
 * of recovery -- this function does not try to tell that apart from any
 * other failure, exactly matching PostgreSQL's own restore_command
 * contract of "nonzero means try the next thing" either way.
 */
bool
ws_restore_run(const WsRestoreTarget *target,
			   const char *filename, const char *outputPath)
{
	return ws_fetch_file_client(target->host, target->port, target->user,
								target->route, target->sslmode,
								filename, outputPath) == 0;
}
