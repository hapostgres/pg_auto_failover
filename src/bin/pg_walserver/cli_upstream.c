/*
 * src/bin/pg_walserver/cli_upstream.c
 *   See cli_upstream.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>

#include "postgres_fe.h"

#include "libpq-fe.h"

#include "cli_upstream.h"
#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "pgsetup.h"
#include "routes.h"
#include "string_utils.h"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))


/*
 * parse_upstream_conninfo parses a plain libpq keyword/value connection
 * string (routes.h's own "upstream" property shape, e.g. "host=primary
 * user=archiver_repl sslmode=require") directly with PQconninfoParse(),
 * filling in target's host/port/user/sslOptions from whatever keywords it
 * finds. Deliberately not this project's own parse_pguri_ssl_settings()
 * (parsing.c): that function's own checkForCompleteURI path is built for
 * postgres:// URIs, not the plain keyword=value shape "upstream" always
 * is here, so a self-contained, direct PQconninfoParse() call is simpler
 * and doesn't risk rejecting a perfectly valid keyword=value string.
 * Password is deliberately never read from here, even if literally present
 * in the string: this project's convention, everywhere a connection string
 * is read from a config file, is PGPASSWORD/.pgpass only, never a password
 * sitting in the file itself.
 */
bool
cli_parse_upstream_conninfo(const char *conninfo, WsUpstreamTarget *target)
{
	char *errmsg = NULL;
	PQconninfoOption *options = PQconninfoParse(conninfo, &errmsg);

	if (options == NULL)
	{
		log_error("Failed to parse upstream connection string \"%s\": %s",
				  conninfo, errmsg == NULL ? "unknown error" : errmsg);
		if (errmsg != NULL)
		{
			PQfreemem(errmsg);
		}
		return false;
	}

	target->node.port = 5432;

	for (PQconninfoOption *option = options; option->keyword != NULL; option++)
	{
		if (option->val == NULL)
		{
			continue;
		}

		if (streq(option->keyword, "host") || streq(option->keyword, "hostaddr"))
		{
			strlcpy(target->node.host, option->val, sizeof(target->node.host));
		}
		else if (streq(option->keyword, "port"))
		{
			(void) stringToInt(option->val, &(target->node.port));
		}
		else if (streq(option->keyword, "user"))
		{
			strlcpy(target->userName, option->val, sizeof(target->userName));
		}
		else if (streq(option->keyword, "sslmode"))
		{
			target->sslOptions.sslMode = pgsetup_parse_sslmode(option->val);
			strlcpy(target->sslOptions.sslModeStr, option->val,
					sizeof(target->sslOptions.sslModeStr));
			target->sslOptions.active = target->sslOptions.sslMode > SSL_MODE_DISABLE;
		}
		else if (streq(option->keyword, "sslrootcert"))
		{
			strlcpy(target->sslOptions.caFile, option->val,
					sizeof(target->sslOptions.caFile));
		}
	}

	PQconninfoFree(options);

	if (target->node.host[0] == '\0')
	{
		log_error("Upstream connection string \"%s\" has no host", conninfo);
		return false;
	}

	return true;
}


/*
 * cli_resolve_upstream -- see cli_upstream.h's own comment.
 */
bool
cli_resolve_upstream(const char *pgdata, const char *configFile,
					 const char *routeKey,
					 const char *pathArg, const char *upstreamArg,
					 const char *hostArg, const char *portArg,
					 const char *userArg,
					 WsUpstreamTarget *target)
{
	memset(target, 0, sizeof(WsUpstreamTarget));
	strlcpy(target->userName, PG_AUTOCTL_REPLICA_USERNAME,
			sizeof(target->userName));

	char routesPath[MAXPGPATH] = { 0 };
	WsRoute *routes = NULL;
	int routeCount = 0;
	const WsRoute *route = NULL;

	if ((pgdata != NULL && pgdata[0] != '\0') ||
		(configFile != NULL && configFile[0] != '\0'))
	{
		config_file_path(pgdata, configFile, routesPath, sizeof(routesPath));

		if (routes_load(routesPath, &routes, &routeCount))
		{
			if (routeKey != NULL && routeKey[0] != '\0')
			{
				route = routes_find(routes, routeCount, routeKey);

				/*
				 * routes_load() itself returns true with zero routes both
				 * for a routes file that doesn't exist yet at all (a
				 * normal, expected state -- nothing to warn about) and for
				 * one that exists but is simply empty or lacks this route
				 * (worth a warning); file_exists() is what tells those two
				 * apart here.
				 */
				if (route == NULL && file_exists(routesPath))
				{
					log_warn("No route \"%s\" in \"%s\"", routeKey, routesPath);
				}

				if (route != NULL && route->disabled)
				{
					log_error("Route \"%s\" is dropped (disabled) -- run "
							  "\"pg_walserver cluster register %s ...\" to "
							  "bring it back first", routeKey, routeKey);
					routes_free(routes);
					return false;
				}
			}
		}
		else
		{
			log_debug("No usable \"%s\" yet", routesPath);
		}
	}

	/* path: explicit --path always wins, else the route's own */
	if (pathArg != NULL && pathArg[0] != '\0')
	{
		strlcpy(target->path, pathArg, sizeof(target->path));
	}
	else if (route != NULL && route->path[0] != '\0')
	{
		strlcpy(target->path, route->path, sizeof(target->path));
	}

	/* upstream: explicit --upstream always wins, else the route's own */
	bool haveUpstream = false;

	if (upstreamArg != NULL && upstreamArg[0] != '\0')
	{
		haveUpstream = cli_parse_upstream_conninfo(upstreamArg, target);
	}
	else if (route != NULL && route->upstream[0] != '\0')
	{
		haveUpstream = cli_parse_upstream_conninfo(route->upstream, target);
	}

	routes_free(routes);

	/* --host/--port/--user, if given, override whatever the above resolved */
	if (hostArg != NULL && hostArg[0] != '\0')
	{
		strlcpy(target->node.host, hostArg, sizeof(target->node.host));
		haveUpstream = true;
	}

	if (portArg != NULL && portArg[0] != '\0')
	{
		(void) stringToInt(portArg, &(target->node.port));
	}
	else if (target->node.port <= 0)
	{
		target->node.port = 5432;
	}

	if (userArg != NULL && userArg[0] != '\0')
	{
		strlcpy(target->userName, userArg, sizeof(target->userName));
	}

	/*
	 * Still nothing? Default to "<pgdata>/<cluster>", the same top-level
	 * storage root every other pg_walserver file already lives under --
	 * --path only ever needs to be passed to override that. When the route
	 * itself already exists in pg_walserver.ini (just without its own
	 * "path" property -- e.g. hand-written with only "upstream"), persist
	 * the default back into the file, the same way "setup" always writes
	 * one for a route it creates, so every other command reading this
	 * route later (routes_load() has no defaulting logic of its own) sees
	 * it too.
	 */
	if (target->path[0] == '\0' && pgdata != NULL && pgdata[0] != '\0' &&
		routeKey != NULL && routeKey[0] != '\0')
	{
		sformat(target->path, sizeof(target->path), "%s/%s", pgdata, routeKey);

		if (route != NULL)
		{
			/* routesPath was already filled in above, resolving the route */
			if (!routes_persist_path(routesPath, routeKey, target->path))
			{
				/* not fatal: the resolved path above is still usable for
				 * this one invocation, only the write-back failed */
				log_warn("Route \"%s\"'s default path could not be saved "
						 "to \"%s\"; pass --path explicitly, or add "
						 "\"path = %s\" to its section by hand, to avoid "
						 "recomputing it every time", routeKey, routesPath,
						 target->path);
			}
		}
	}

	if (target->path[0] == '\0')
	{
		log_error("No route path: pass --path, or --cluster with --pgdata "
				  "pointing at a \"pg_walserver.ini\" that has one");
		return false;
	}

	if (!haveUpstream || target->node.host[0] == '\0')
	{
		log_error("No upstream to connect to: pass --host (with --port/"
				  "--user), or --upstream, or --cluster with --pgdata "
				  "pointing at a \"pg_walserver.ini\" whose route has an "
				  "\"upstream\" property");
		return false;
	}

	return true;
}
