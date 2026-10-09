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
 * ws_prefill_pgdata_from_env fills pgdata from the PGDATA environment
 * variable, before this sub-command's own getopt loop parses --pgdata
 * (which then overrides whatever this wrote, if given). PGDATA being
 * unset here is entirely normal -- every caller only ever uses this as an
 * optional default, never a requirement -- so this deliberately does NOT
 * call env_utils.c's own get_env_pgdata(): that function unconditionally
 * log_error()s when the variable is unset (right, for its own other
 * callers in this codebase, e.g. pidfile.c's own create_pidfile(), which
 * genuinely cannot proceed without it), which would otherwise print a
 * scary, misleading ERROR on every single pg_walserver invocation that
 * passes --pgdata explicitly and simply never has PGDATA set in its
 * environment at all -- the common case for a cron job or a one-off
 * command. pgdata must be at least MAXPGPATH bytes.
 */
void
ws_prefill_pgdata_from_env(char *pgdata)
{
	if (env_exists("PGDATA"))
	{
		(void) get_env_copy("PGDATA", pgdata, MAXPGPATH);
	}
}
