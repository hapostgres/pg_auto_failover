/*
 * src/bin/pg_walserver/cli_cluster.c
 *   See cli_cluster.h. "pg_walserver cluster register <name>", "pg_
 *   walserver cluster drop <name>", "pg_walserver cluster list", "pg_
 *   walserver cluster set-upstream <name>": registering, dropping,
 *   listing, and re-pointing the clusters (routes) one pg_walserver
 *   instance archives -- split out of what used to be "pg_walserver
 *   setup" (now cli_setup.c, narrowed to configuring pg_walserver itself:
 *   --pgdata's own port/TLS/auth-timeout defaults, nothing about any one
 *   cluster). "setup" configures the server; these configure what it
 *   serves.
 *
 *   "cluster register <name>", in order, stopping at the first failure:
 *
 *     1. resolve path/upstream (cli_upstream.c: --path/--pguri/--host/
 *        --port/--user, or <name> looked up in the config file when it
 *        already has a matching section);
 *     2. write (or validate) the config file's own section for <name>,
 *        refusing a route key that already exists with a *different*
 *        path/upstream unless --force -- the same overwrite-safety
 *        principle as cli_fetch_systemid.c's own systemid check, applied
 *        one layer up; the embedded receivewal worker is opted into by
 *        *default* now (an explicit "receivewal = pull" is written into the
 *        route's own section, routes.h, unless --no-receivewal / --receivewal
 *        none says otherwise), opting the route into it the next time
 *        "serve" starts -- "cluster register" itself never starts or
 *        touches that receivewal worker, it only records the intent.
 *        Writing the property explicitly (rather than changing what an
 *        *absent* "receivewal" property under a hand-edited config file
 *        means, which stays "off", unchanged in routes.c/routes.h) is a
 *        deliberate choice: anyone reading the config file by hand sees
 *        exactly what "cluster register" decided, with no implicit-
 *        default surprise to remember;
 *     3. fetch the system identifier (cli_fetch_systemid.c) -- this
 *        connection (pgctl_identify_system(), a real replication-mode
 *        IDENTIFY_SYSTEM) is also this step's own role-permission check:
 *        Postgres refuses a replication-mode connection for a role lacking
 *        REPLICATION at the *backend* level, independent of HBA, so a
 *        misconfigured role fails here with a clear message instead of a
 *        cryptic pg_basebackup/pg_receivewal error later;
 *     4. once every route in the config file is accounted for, create a
 *        self-signed certificate for <pgdata> if none exists yet
 *        (pg_create_self_signed_cert(), the exact function `pg_autoctl
 *        create ... --ssl-self-signed` already uses), in either of two
 *        cases: there is now more than one route (TLS becomes mandatory
 *        the moment dbname-based routing stops being reliable for a real
 *        physical standby -- see auth.c's own comment and README.md's
 *        "Routing beyond dbname: TLS SNI" section), or --ssl-self-signed
 *        was given explicitly, whether or not this is the only route.
 *        Warns (never refuses) if --hostname was never given for a route
 *        now sharing the file with others;
 *     5. reload an already-running "pg_walserver serve" for this same
 *        --pgdata, if one is running (cli_root.c's own cli_cluster_
 *        reload_running_server()) -- the same read_pidfile()/SIGHUP shape
 *        "pg_walserver reload" itself uses. "cluster register" never
 *        takes a base backup itself: a running server picks up the
 *        new/changed route the moment it is reloaded, and bootstraps a
 *        first base backup for it automatically if it doesn't have one
 *        yet.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "pqexpbuffer.h"

#include "cli_basebackup.h"
#include "cli_cluster.h"
#include "cli_create_cert.h"
#include "cli_fetch_systemid.h"
#include "cli_upstream.h"
#include "file_utils.h"
#include "log.h"
#include "routes.h"
#include "string_utils.h"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))


/*
 * write_route_section creates or validates the [routeKey] section of the
 * config file: a brand new key is appended; an existing one must already
 * have the same path (an operator re-running "cluster register" must be
 * a safe no-op), or --force is required to change it -- the same "never
 * silently replace what's already there" principle as cli_fetch_
 * systemid.c's own systemid check.
 */
static bool
write_route_section(const char *configPath, const char *routeKey,
					const WsUpstreamTarget *target, const char *upstreamRaw,
					const char *hostname, bool receivewalPull, bool force)
{
	WsRoute *routes = NULL;
	int routeCount = 0;
	bool haveExisting = routes_load(configPath, &routes, &routeCount);
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
				  routeKey, configPath, existing->path, target->path);
		routes_free(routes);
		return false;
	}

	routes_free(routes);

	if (existing != NULL && streq(existing->path, target->path))
	{
		log_info("Route \"%s\" already configured in \"%s\"",
				 routeKey, configPath);

		if (receivewalPull && !existing->receivewalPull)
		{
			log_warn("Route \"%s\" already exists in \"%s\" without "
					 "\"receivewal = pull\" (the embedded receivewal worker is on "
					 "by default now, but was not the last time \"register "
					 "cluster\" wrote this route, or --no-receivewal/--receivewal "
					 "none was passed then) -- edit \"%s\" by hand to add it, "
					 "\"cluster register\" never changes an already-existing "
					 "route's properties beyond path", routeKey, configPath,
					 configPath);
		}

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

	if (receivewalPull)
	{
		appendPQExpBufferStr(section, "receivewal = pull\n");
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

	if (read_file_if_exists(configPath, &existingContents, &existingSize) &&
		existingContents != NULL)
	{
		PQExpBuffer whole = createPQExpBuffer();

		appendPQExpBufferStr(whole, existingContents);
		appendPQExpBufferStr(whole, section->data);

		ok = !PQExpBufferBroken(whole) &&
			 write_file_atomic(whole->data, whole->len, configPath);

		destroyPQExpBuffer(whole);
		free(existingContents);
	}
	else
	{
		/* skip the leading blank line for a brand new file */
		ok = write_file_atomic(section->data + 1, section->len - 1, configPath);
	}

	destroyPQExpBuffer(section);

	if (!ok)
	{
		log_error("Failed to write \"%s\"", configPath);
		return false;
	}

	log_info("Added route \"%s\" (path \"%s\") to \"%s\"",
			 routeKey, target->path, configPath);

	return true;
}


/*
 * ensure_tls_certificate re-reads the config file after write_route_
 * section() and creates a self-signed certificate for <pgdata> (via ws_
 * create_cert_run() -- cli_create_cert.c, the same helper `pg_walserver
 * create-cert` itself calls, wrapping pg_create_self_signed_cert()) in
 * either of two cases: the file now holds more than one route, or
 * sslSelfSigned (--ssl-self-signed) was given explicitly, whether or not
 * this is the file's only route. Either way, an already-existing
 * certificate is left untouched; the certificate's own CN is routeKey's
 * own --hostname when one was given, else this machine's own hostname.
 * Also warns when routeKey itself has no "hostname" property in the
 * multi-route case. Never a hard failure in either case.
 */
static void
ensure_tls_certificate(const char *pgdata, const char *configPath,
					   const char *routeKey, const char *hostname,
					   bool sslSelfSigned)
{
	WsRoute *routes = NULL;
	int routeCount = 0;

	if (!routes_load(configPath, &routes, &routeCount))
	{
		routes_free(routes);
		return;
	}

	routes_free(routes);

	bool haveHostname = hostname != NULL && hostname[0] != '\0';
	bool multipleRoutes = routeCount > 1;

	if (multipleRoutes)
	{
		log_info("\"%s\" now has %d routes: TLS is required for more than "
				 "one route to be reachable by name (dbname-based routing "
				 "alone cannot tell a real physical standby's connection "
				 "apart from any other route once there is more than one, "
				 "see this project's own README.md)",
				 configPath, routeCount);
	}

	if (!multipleRoutes && !sslSelfSigned)
	{
		return;
	}

	char certPath[MAXPGPATH] = { 0 };
	char keyPath[MAXPGPATH] = { 0 };

	sformat(certPath, sizeof(certPath), "%s/server.crt", pgdata);
	sformat(keyPath, sizeof(keyPath), "%s/server.key", pgdata);

	if (!file_exists(certPath) || !file_exists(keyPath))
	{
		char localHostname[_POSIX_HOST_NAME_MAX] = "pg_walserver";

		if (!haveHostname)
		{
			(void) gethostname(localHostname, sizeof(localHostname));
		}

		if (!ws_create_cert_run(pgdata, haveHostname ? hostname : localHostname,
								false))
		{
			log_warn("Failed to create a self-signed certificate for "
					 "\"%s\" -- pass --ssl-cert-file/--ssl-key-file to "
					 "\"serve\", or create \"%s\"/\"%s\" yourself (\"pg_"
					 "walserver create-cert\"), before starting it",
					 pgdata, certPath, keyPath);
		}
	}

	if (multipleRoutes && !haveHostname)
	{
		log_warn("Route \"%s\" has no --hostname: it can only be reached "
				 "by dbname (pg_basebackup/pg_receivewal/archive_command) "
				 "or the \"*\" wildcard, never by name by a real physical "
				 "standby -- pass --hostname next time, or edit \"%s\" by "
				 "hand, to add one", routeKey, configPath);
	}
}


/*
 * ws_cluster_register_run -- see cli_cluster.h's own comment, and this
 * file's own header comment for the full sequence.
 */
bool
ws_cluster_register_run(const WsClusterRegisterOptions *options)
{
	if (options->cluster[0] == '\0')
	{
		log_error("cluster register requires a cluster name "
				  "(\"pg_walserver cluster register <name> ...\")");
		return false;
	}

	if (options->pgdata[0] == '\0')
	{
		log_error("cluster register requires --pgdata (this instance's "
				  "own data root)");
		return false;
	}

	char configPath[MAXPGPATH] = { 0 };

	config_file_path(options->pgdata, options->configFile,
					 configPath, sizeof(configPath));

	/*
	 * --path is only ever an override: a route's own directory defaults to
	 * "<pgdata>/<cluster>", the same top-level storage root every other
	 * pg_walserver file already lives under.
	 */
	char defaultPath[MAXPGPATH] = { 0 };
	const char *pathArg = options->path;

	if (pathArg[0] == '\0')
	{
		sformat(defaultPath, sizeof(defaultPath), "%s/%s",
				options->pgdata, options->cluster);
		pathArg = defaultPath;
	}

	WsUpstreamTarget target = { 0 };

	if (!cli_resolve_upstream(NULL /* not looking one up yet */, NULL, NULL,
							  pathArg, options->pguri,
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
	 * captured WAL and prior base backups on a re-run). Just make sure it
	 * exists; never touch what's already in it.
	 */
	if (!directory_exists(target.path) &&
		pg_mkdir_p((char *) target.path, 0700) == -1)
	{
		log_error("Failed to create \"%s\": %m", target.path);
		return false;
	}

	/*
	 * cli_resolve_upstream() above already refused to succeed without a
	 * resolved host (--pguri, --host, or an existing route's own
	 * "upstream"), so --receivewal pull always has somewhere to pull from by
	 * the time it's written below -- no separate check needed here.
	 */
	if (!write_route_section(configPath, options->cluster, &target,
							 options->pguri, options->hostname,
							 options->receivewalPull, options->force))
	{
		/* errors have already been logged */
		return false;
	}

	ensure_tls_certificate(options->pgdata, configPath, options->cluster,
						   options->hostname, options->sslSelfSigned);

	uint64_t systemIdentifier = 0;

	if (!cli_fetch_systemid_run(&target, options->force, &systemIdentifier))
	{
		/* errors have already been logged -- including, per this call's own
		 * comment, a role lacking REPLICATION: Postgres refuses a
		 * replication-mode connection for that at the backend level */
		return false;
	}

	log_info("cluster register complete: route \"%s\" is ready (no base "
			 "backup taken here -- \"pg_walserver serve\" bootstraps the "
			 "route's first base backup automatically, once, the next "
			 "time it starts or reloads this route; run \"pg_walserver "
			 "basebackup\" by hand at any time to take another one)",
			 options->cluster);

	return true;
}


/*
 * ws_cluster_drop_run removes routeKey's own [section] from the config
 * file (routes_drop_section(), routes.c) -- the registration only. The
 * route's own on-disk data (captured WAL, base backups, under its own
 * "path") is left untouched unless purge is true, in which case it is
 * rmtree()'d after the section is gone: a deliberate two-step, opt-in
 * destruction, never a side effect of dropping the registration alone --
 * an operator who only meant to stop archiving a cluster, or is about to
 * re-register it under a different upstream, should never lose its
 * archive by accident.
 */
bool
ws_cluster_drop_run(const char *pgdata, const char *configFile,
					const char *routeKey, bool purge)
{
	if (routeKey == NULL || routeKey[0] == '\0')
	{
		log_error("cluster drop requires a cluster name "
				  "(\"pg_walserver cluster drop <name> ...\")");
		return false;
	}

	if (pgdata == NULL || pgdata[0] == '\0')
	{
		log_error("cluster drop requires --pgdata (this instance's own "
				  "data root)");
		return false;
	}

	char configPath[MAXPGPATH] = { 0 };

	config_file_path(pgdata, configFile, configPath, sizeof(configPath));

	WsRoute *routes = NULL;
	int routeCount = 0;

	if (!routes_load(configPath, &routes, &routeCount))
	{
		/* errors have already been logged */
		return false;
	}

	const WsRoute *route = routes_find_exact(routes, routeCount, routeKey);

	if (route == NULL)
	{
		log_error("No route \"%s\" in \"%s\"", routeKey, configPath);
		routes_free(routes);
		return false;
	}

	char routePath[MAXPGPATH] = { 0 };

	strlcpy(routePath, route->path, sizeof(routePath));
	routes_free(routes);

	if (!routes_drop_section(configPath, routeKey))
	{
		/* errors have already been logged */
		return false;
	}

	if (purge)
	{
		if (!rmtree(routePath, true))
		{
			log_warn("Route \"%s\" was dropped from \"%s\", but removing "
					 "its own directory \"%s\" failed -- remove it by "
					 "hand", routeKey, configPath, routePath);
		}
		else
		{
			log_info("Removed \"%s\" (--purge)", routePath);
		}
	}
	else
	{
		log_info("Route \"%s\" dropped from \"%s\"; its own data under "
				 "\"%s\" was left in place (pass --purge to remove it too)",
				 routeKey, configPath, routePath);
	}

	return true;
}


/*
 * ws_cluster_list_run prints one row per registered route: its key, path,
 * upstream, hostname (or "-"), and "receivewal" setting -- the
 * registration itself, as the config file records it, never the
 * operational/data-layer facts :ref:`pg_walserver_list`'s own "list
 * clusters" already reports (backup/WAL presence, WAL range). Prints a
 * clean "no clusters registered yet" message, not an error, when there
 * are none.
 */
bool
ws_cluster_list_run(const char *pgdata, const char *configFile,
					bool showUpstream)
{
	if ((pgdata == NULL || pgdata[0] == '\0') &&
		(configFile == NULL || configFile[0] == '\0'))
	{
		log_error("cluster list requires --pgdata or --config");
		return false;
	}

	char configPath[MAXPGPATH] = { 0 };

	config_file_path(pgdata, configFile, configPath, sizeof(configPath));

	WsRoute *routes = NULL;
	int routeCount = 0;

	if (!routes_load(configPath, &routes, &routeCount))
	{
		/* errors have already been logged */
		return false;
	}

	if (routeCount == 0)
	{
		printf("No clusters registered yet under \"%s\" -- see " /* IGNORE-BANNED */
			   "\"pg_walserver cluster register\".\n", configPath);
		routes_free(routes);
		return true;
	}

	if (showUpstream)
	{
		/*
		 * UPSTREAM values are full connection strings/URIs, often far
		 * wider than any other column -- a wide row layout is unreadable
		 * once they're included, so pivot to one key: value block per
		 * cluster instead (only reached with --upstream, an explicit
		 * opt-in; the default table below never prints this column).
		 */
		for (int i = 0; i < routeCount; i++)
		{
			if (i > 0)
			{
				printf("\n"); /* IGNORE-BANNED */
			}

			printf("cluster:    %s\n", routes[i].key); /* IGNORE-BANNED */
			printf("receivewal: %s\n", /* IGNORE-BANNED */
				   routes[i].receivewalPull ? "pull" : "none");
			printf("upstream:   %s\n", /* IGNORE-BANNED */
				   routes[i].upstream[0] != '\0' ? routes[i].upstream : "-");
			printf("hostname:   %s\n", /* IGNORE-BANNED */
				   routes[i].hostname[0] != '\0' ? routes[i].hostname : "-");
			printf("path:       %s\n", routes[i].path); /* IGNORE-BANNED */
		}

		routes_free(routes);

		return true;
	}

	printf("%-20s %-10s %-24s %s\n", /* IGNORE-BANNED */
		   "CLUSTER", "RECEIVEWAL", "HOSTNAME", "PATH");
	printf("%-20s %-10s %-24s %s\n", /* IGNORE-BANNED */
		   "--------------------", "----------",
		   "------------------------", "----");

	for (int i = 0; i < routeCount; i++)
	{
		printf("%-20s %-10s %-24s %s\n", /* IGNORE-BANNED */
			   routes[i].key,
			   routes[i].receivewalPull ? "pull" : "none",
			   routes[i].hostname[0] != '\0' ? routes[i].hostname : "-",
			   routes[i].path);
	}

	routes_free(routes);

	return true;
}


/*
 * ws_cluster_set_upstream_run changes routeKey's own "upstream" property
 * (routes_set_property(), routes.c) -- the way to point an already-
 * registered cluster at a new upstream after a failover (or a planned
 * move), without dropping and re-registering it. An already-running
 * "serve" for the same --pgdata is reloaded immediately afterward
 * (cli_root.c's own cli_cluster_reload_running_server(), the exact same
 * shape "cluster register" and "reload" itself already use): reload's
 * own reconciliation (receivewal.c's own ws_receivewal_reload()) already
 * detects an "upstream" change on its own and restarts this route's
 * embedded receivewal worker against the new one -- no separate "move the
 * receivewal worker" step needed here, reload already does it. With
 * forceBasebackup, also takes a fresh base backup against the *new*
 * upstream right away (cli_basebackup_run(), cli_basebackup.c) once the
 * property is written -- the common "failover just happened, the old
 * upstream's own base backups are no longer usable against the new
 * timeline, get a current one immediately" case -- rather than leaving a
 * stale backup in place until the next scheduled "pg_walserver
 * basebackup" call.
 */
bool
ws_cluster_set_upstream_run(const char *pgdata, const char *configFile,
							const char *routeKey, const char *newUpstream,
							bool forceBasebackup)
{
	if (routeKey == NULL || routeKey[0] == '\0')
	{
		log_error("cluster set-upstream requires a cluster name "
				  "(\"pg_walserver cluster set-upstream <name> ...\")");
		return false;
	}

	if (pgdata == NULL || pgdata[0] == '\0')
	{
		log_error("cluster set-upstream requires --pgdata (this "
				  "instance's own data root)");
		return false;
	}

	if (newUpstream == NULL || newUpstream[0] == '\0')
	{
		log_error("cluster set-upstream requires --pguri");
		return false;
	}

	char configPath[MAXPGPATH] = { 0 };

	config_file_path(pgdata, configFile, configPath, sizeof(configPath));

	WsRoute *routes = NULL;
	int routeCount = 0;

	if (!routes_load(configPath, &routes, &routeCount))
	{
		/* errors have already been logged */
		return false;
	}

	const WsRoute *route = routes_find_exact(routes, routeCount, routeKey);

	if (route == NULL)
	{
		log_error("No route \"%s\" in \"%s\"", routeKey, configPath);
		routes_free(routes);
		return false;
	}

	char routePath[MAXPGPATH] = { 0 };

	strlcpy(routePath, route->path, sizeof(routePath));
	routes_free(routes);

	if (!routes_set_property(configPath, routeKey, "upstream", newUpstream))
	{
		/* errors have already been logged */
		return false;
	}

	/*
	 * The upstream's own major version, and system identifier, may well
	 * have changed too (a failover to a different PostgreSQL version
	 * during an upgrade, most notably) -- re-fetch both against the new
	 * upstream right away, --force since the whole point of this command
	 * is that they are now expected to change, not an error to refuse.
	 */
	WsUpstreamTarget target = { 0 };

	if (cli_resolve_upstream(pgdata, configFile, routeKey, NULL, NULL, NULL,
							 NULL, NULL, &target))
	{
		uint64_t systemIdentifier = 0;

		if (!cli_fetch_systemid_run(&target, true, &systemIdentifier))
		{
			log_warn("Route \"%s\"'s own system identifier/upstream "
					 "version could not be refreshed against the new "
					 "upstream -- run \"pg_walserver fetch-systemid "
					 "--cluster %s --force\" by hand", routeKey, routeKey);
		}
	}

	log_info("Route \"%s\"'s own upstream is now \"%s\"", routeKey,
			 newUpstream);

	if (forceBasebackup)
	{
		WsUpstreamTarget backupTarget = { 0 };

		if (!cli_resolve_upstream(pgdata, configFile, routeKey, NULL, NULL,
								  NULL, NULL, NULL, &backupTarget) ||
			!cli_basebackup_run(&backupTarget, NULL, 0))
		{
			log_error("Route \"%s\"'s own upstream was updated, but the "
					  "base backup that --force-basebackup asked for "
					  "against it did not complete -- see the error(s) "
					  "logged above; run \"pg_walserver basebackup "
					  "--cluster %s\" by hand", routeKey, routeKey);
			return false;
		}
	}

	return true;
}
