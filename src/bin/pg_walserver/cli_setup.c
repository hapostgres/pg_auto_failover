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
 *        "a background job was started that might still be running";
 *     5. once every route in pg_walserver.ini is accounted for, if there is
 *        now more than one: TLS becomes mandatory (a single-route server
 *        works with or without it, dbname alone is unambiguous; with
 *        several routes, dbname-based routing stops being reliable at all
 *        for a real physical standby -- see auth.c's own comment and
 *        DESIGN-standalone-archiving.md's "Routing beyond dbname" section
 *        -- so TLS SNI becomes the only way to address more than one route
 *        by name). setup creates a self-signed certificate for <pgdata> if
 *        none exists yet (pg_create_self_signed_cert(), the exact function
 *        `pg_autoctl create archiver --ssl-self-signed` already uses), and
 *        warns if --hostname was never given for a route now sharing the
 *        file with others -- that route can then only ever be reached by
 *        dbname (fine for pg_basebackup/pg_receivewal/archive_command,
 *        never for a real physical standby) or the "*" wildcard.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "pqexpbuffer.h"

#include "cli_basebackup.h"
#include "cli_fetch_systemid.h"
#include "cli_setup.h"
#include "cli_upstream.h"
#include "file_utils.h"
#include "log.h"
#include "pgctl.h"
#include "pgsetup.h"
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
					const char *hostname, bool force)
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

	if (hostname != NULL && hostname[0] != '\0')
	{
		appendPQExpBuffer(section, "hostname = %s\n", hostname);
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


/*
 * ensure_tls_for_multiple_routes re-reads pg_walserver.ini after
 * write_route_section() and, when it now holds more than one route,
 * makes sure a certificate exists for <pgdata> (creating a self-signed
 * one with pg_create_self_signed_cert() -- the exact function `pg_autoctl
 * create archiver --ssl-self-signed` already uses -- when neither
 * server.crt/server.key nor an already-loaded certificate is there), and
 * warns when routeKey itself has no "hostname" property: without one, it
 * can only ever be reached by dbname (fine for pg_basebackup/pg_
 * receivewal/archive_command, never for a real physical standby, see
 * auth.c's own comment) or the "*" wildcard. Never a hard failure -- a
 * single-route deployment (the common case) never reaches any of this at
 * all, and even a multi-route one that only ever serves pg_basebackup/
 * pg_receivewal/archive_command by dbname genuinely doesn't need TLS/SNI,
 * so this only warns, it does not refuse to proceed.
 */
static void
ensure_tls_for_multiple_routes(const char *pgdata, const char *routeKey,
							   bool haveHostname)
{
	char routesPath[MAXPGPATH] = { 0 };
	WsRoute *routes = NULL;
	int routeCount = 0;

	sformat(routesPath, sizeof(routesPath), "%s/pg_walserver.ini", pgdata);

	if (!routes_load(routesPath, &routes, &routeCount) || routeCount <= 1)
	{
		routes_free(routes);
		return;
	}

	routes_free(routes);

	log_info("\"%s\" now has %d routes: TLS is required for more than one "
			 "route to be reachable by name (dbname-based routing alone "
			 "cannot tell a real physical standby's connection apart from "
			 "any other route once there is more than one, see this "
			 "project's own DESIGN-standalone-archiving.md)",
			 routesPath, routeCount);

	char certPath[MAXPGPATH] = { 0 };
	char keyPath[MAXPGPATH] = { 0 };

	sformat(certPath, sizeof(certPath), "%s/server.crt", pgdata);
	sformat(keyPath, sizeof(keyPath), "%s/server.key", pgdata);

	if (!file_exists(certPath) || !file_exists(keyPath))
	{
		PostgresSetup pgSetup = { 0 };
		char localHostname[_POSIX_HOST_NAME_MAX] = "pg_walserver";

		(void) gethostname(localHostname, sizeof(localHostname));
		strlcpy(pgSetup.pgdata, pgdata, sizeof(pgSetup.pgdata));

		if (pg_create_self_signed_cert(&pgSetup, localHostname))
		{
			log_info("Created a self-signed certificate for \"%s\" "
					 "(\"%s\"/\"%s\") -- replace it with a real one before "
					 "running on a reachable network", pgdata, certPath,
					 keyPath);
		}
		else
		{
			log_warn("Failed to create a self-signed certificate for "
					 "\"%s\" -- pass --ssl-cert-file/--ssl-key-file to "
					 "\"serve\", or create \"%s\"/\"%s\" yourself, before "
					 "starting it", pgdata, certPath, keyPath);
		}
	}

	if (!haveHostname)
	{
		log_warn("Route \"%s\" has no --hostname: it can only be reached "
				 "by dbname (pg_basebackup/pg_receivewal/archive_command) "
				 "or the \"*\" wildcard, never by name by a real physical "
				 "standby -- pass --hostname next time, or edit \"%s\" by "
				 "hand, to add one", routeKey, routesPath);
	}
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
							 options->upstream, options->hostname,
							 options->force))
	{
		/* errors have already been logged */
		return false;
	}

	ensure_tls_for_multiple_routes(options->pgdata, options->route,
								   options->hostname[0] != '\0');

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
