/*
 * src/bin/pg_walserver/cli_setup.c
 *   See cli_setup.h.
 *
 *   The sequence, in order, stopping at the first failure:
 *
 *     1. resolve path/upstream (cli_upstream.c: --path/--upstream/--host/
 *        --port/--user, or --route looked up in <pgdata>/pg_walserver.ini
 *        when it already has a matching section);
 *     2. write (or validate) the pg_walserver.ini section for --route,
 *        refusing a route key that already exists with a *different*
 *        path/upstream unless --force -- the same overwrite-safety
 *        principle as cli_fetch_systemid.c's own systemid check, applied
 *        one layer up;
 *     3. fetch the system identifier (cli_fetch_systemid.c) -- this
 *        connection (pgctl_identify_system(), a real replication-mode
 *        IDENTIFY_SYSTEM) is also this step's own role-permission check:
 *        Postgres refuses a replication-mode connection for a role lacking
 *        REPLICATION at the *backend* level, independent of HBA, so a
 *        misconfigured role fails here with a clear message instead of a
 *        cryptic pg_basebackup/pg_receivewal error later. An earlier
 *        version of this file ran a separate plain-SQL "SELECT
 *        rolreplication FROM pg_roles" check first -- removed: that
 *        connection targets an ordinary database (postgres), which the
 *        replication role's own HBA rule usually does not admit at all
 *        (pg_auto_failover's own bootstrap scopes it to replication-only
 *        connections), so the "extra" check failed even when the role was
 *        perfectly fine, on a false premise;
 *     4. with --with-basebackup, take the first base backup
 *        (cli_basebackup.c) -- synchronously: setup does not return until
 *        it has actually succeeded or failed, on the theory that "setup
 *        finished" should mean the route is genuinely ready to serve, not
 *        "a background job was started that might still be running".
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>

#include "postgres_fe.h"

#include "pqexpbuffer.h"

#include "cli_basebackup.h"
#include "cli_fetch_systemid.h"
#include "cli_setup.h"
#include "cli_upstream.h"
#include "file_utils.h"
#include "log.h"
#include "routes.h"
#include "string_utils.h"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))


/*
 * write_route_section creates or validates the [routeKey] section of
 * pg_walserver.ini: a brand new key is appended; an existing one must
 * already have the same path (an operator re-running setup, or setup
 * --with-basebackup after an earlier setup without it, must be a safe
 * no-op), or --force is required to change it -- the same "never silently
 * replace what's already there" principle as cli_fetch_systemid.c's own
 * systemid check.
 */
static bool
write_route_section(const char *pgdata, const char *routeKey,
					const WsUpstreamTarget *target, const char *upstreamRaw,
					bool force)
{
	char routesPath[MAXPGPATH] = { 0 };

	sformat(routesPath, sizeof(routesPath), "%s/pg_walserver.ini", pgdata);

	WsRoute *routes = NULL;
	int routeCount = 0;
	bool haveExisting = routes_load(routesPath, &routes, &routeCount);
	const WsRoute *existing = haveExisting
		? routes_find(routes, routeCount, routeKey)
		: NULL;

	if (existing != NULL && !streq(existing->key, routeKey))
	{
		/* the "*" wildcard can be found for a key that isn't literally
		 * "*" -- never treat that as "the route already exists" here */
		existing = NULL;
	}

	if (existing != NULL && !streq(existing->path, target->path) && !force)
	{
		log_error("Route \"%s\" already exists in \"%s\" with path \"%s\", "
				  "not \"%s\" -- pass --force to change it",
				  routeKey, routesPath, existing->path, target->path);
		routes_free(routes);
		return false;
	}

	routes_free(routes);

	if (existing != NULL && streq(existing->path, target->path))
	{
		log_info("Route \"%s\" already configured in \"%s\"",
				 routeKey, routesPath);
		return true;
	}

	PQExpBuffer section = createPQExpBuffer();

	appendPQExpBuffer(section, "\n[%s]\npath = %s\n", routeKey, target->path);

	if (upstreamRaw != NULL && upstreamRaw[0] != '\0')
	{
		appendPQExpBuffer(section, "upstream = %s\n", upstreamRaw);
	}

	if (PQExpBufferBroken(section))
	{
		destroyPQExpBuffer(section);
		log_error("Out of memory");
		return false;
	}

	char *existingContents = NULL;
	long existingSize = 0L;
	bool ok;

	if (read_file_if_exists(routesPath, &existingContents, &existingSize) &&
		existingContents != NULL)
	{
		PQExpBuffer whole = createPQExpBuffer();

		appendPQExpBufferStr(whole, existingContents);
		appendPQExpBufferStr(whole, section->data);

		ok = !PQExpBufferBroken(whole) &&
			 write_file_atomic(whole->data, whole->len, routesPath);

		destroyPQExpBuffer(whole);
		free(existingContents);
	}
	else
	{
		/* skip the leading blank line for a brand new file */
		ok = write_file_atomic(section->data + 1, section->len - 1, routesPath);
	}

	destroyPQExpBuffer(section);

	if (!ok)
	{
		log_error("Failed to write \"%s\"", routesPath);
		return false;
	}

	log_info("Added route \"%s\" (path \"%s\") to \"%s\"",
			 routeKey, target->path, routesPath);

	return true;
}


bool
cli_setup_run(const WsSetupOptions *options)
{
	if (options->route[0] == '\0' || options->path[0] == '\0')
	{
		log_error("setup requires --route and --path");
		return false;
	}

	if (options->pgdata[0] == '\0')
	{
		log_error("setup requires --pgdata (where pg_walserver.ini lives)");
		return false;
	}

	WsUpstreamTarget target = { 0 };

	if (!cli_resolve_upstream(NULL /* not looking one up yet */, NULL,
							  options->path, options->upstream,
							  options->host,
							  options->port[0] != '\0' ? options->port : NULL,
							  options->user, &target))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * Deliberately NOT ensure_empty_dir(): that function rmtree()s an
	 * *existing* directory before recreating it (right for a base backup's
	 * own one-shot destination in cli_basebackup.c, catastrophic here --
	 * this is the route's top-level directory, potentially already holding
	 * captured WAL and prior base backups on a re-run of setup). Just make
	 * sure it exists; never touch what's already in it.
	 */
	if (!directory_exists(target.path) &&
		pg_mkdir_p((char *) target.path, 0700) == -1)
	{
		log_error("Failed to create \"%s\": %m", target.path);
		return false;
	}

	if (!write_route_section(options->pgdata, options->route, &target,
							 options->upstream, options->force))
	{
		/* errors have already been logged */
		return false;
	}

	uint64_t systemIdentifier = 0;

	if (!cli_fetch_systemid_run(&target, options->force, &systemIdentifier))
	{
		/* errors have already been logged -- including, per this call's own
		 * comment, a role lacking REPLICATION: Postgres refuses a
		 * replication-mode connection for that at the backend level */
		return false;
	}

	if (options->withBasebackup)
	{
		char label[NAMEDATALEN] = { 0 };

		log_info("Taking the route's first base backup (this may take a "
				 "while; setup will not return until it completes)");

		if (!cli_basebackup_run(&target, label, sizeof(label)))
		{
			/* errors have already been logged */
			return false;
		}

		log_info("setup complete: route \"%s\" is ready, base backup \"%s\"",
				 options->route, label);
	}
	else
	{
		log_info("setup complete: route \"%s\" is ready (no base backup "
				 "taken -- pass --with-basebackup, or run "
				 "\"pg_walserver basebackup\" separately, before serving "
				 "BASE_BACKUP requests for it)",
				 options->route);
	}

	return true;
}
