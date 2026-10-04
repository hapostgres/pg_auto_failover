/*
 * src/bin/pg_walserver/cli_common.c
 *   See cli_common.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include "postgres_fe.h"

#include "cli_common.h"
#include "env_utils.h"

/*
 * ws_prefill_pgdata_from_env -- see cli_common.h.
 */
void
ws_prefill_pgdata_from_env(char *pgdata)
{
	if (env_exists("PGDATA"))
	{
		(void) get_env_copy("PGDATA", pgdata, MAXPGPATH);
	}
}
